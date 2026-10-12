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
"""What the view of an App::Part does with what the Part holds.

Two things, each of which was wrong with nothing but stock objects.

Where a plain group inside the Part is selected. A selection, a
preselection, a show on top and a view's own visibility are all drawn on the
nodes ViewProvider.getDetailPath() ends at. The Part lays everything it
holds flat under one node, a plain group's children beside the group
(ViewParams LinkChildrenDirect, the default), and the path of the group
ended at that node: the group selected lit the whole Part, and the group
hidden in one view hid the whole Part.

And a Part that was only opened. Its origin is sized to what it holds a
moment after the load (Gui::ViewProviderOriginGroupExtension), which is
fitted to the model and no edit: the document nobody touched was a modified
one, and closing it asked to save.

Needs the GUI: run it through scripts/sandbox-gui-gate.py, or in the running
application. Skips headless.
"""

import os
import tempfile
import time
import unittest

import FreeCAD

App = FreeCAD


class PartGroupPathCases(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        self.Doc = App.newDocument("PartGroupView")
        self.Part = self.Doc.addObject("App::Part", "Part")
        self.Group = self.Doc.addObject("App::DocumentObjectGroup", "Group")
        self.Part.addObject(self.Group)
        for i in range(2):
            box = self.Doc.addObject("Part::Box", "Box%d" % i)
            box.Placement.Base = App.Vector(20 * i, 0, 0)
            self.Group.addObject(box)
        self.Part.addObject(self.Doc.addObject("Part::Box", "Out"))
        self.Top = self.Doc.addObject("App::DocumentObjectGroup", "Top")
        self.Top.addObject(self.Doc.addObject("Part::Box", "TopBox"))
        self.Doc.recompute()

    def tearDown(self):
        App.closeDocument(self.Doc.Name)

    def path(self, obj, sub):
        """The objects whose root node is on the path, and the kind of node
        the path ends at"""
        from pivy import coin

        path = coin.SoPath()
        obj.ViewObject.getDetailPath(sub, path, True)
        nodes = [path.getNode(i) for i in range(path.getLength())]
        roots = {}
        for o in self.Doc.Objects:
            roots[o.ViewObject.RootNode.this.__int__()] = o.Name
        names = [roots[n.this.__int__()] for n in nodes if n.this.__int__() in roots]
        return names, nodes

    def testGroupInAPartIsItsOwnNodes(self):
        names, nodes = self.path(self.Part, "Group.")
        self.assertEqual(names, ["Part", "Group"])
        # as far down as the path of the group anywhere else
        top, topNodes = self.path(self.Top, "")
        self.assertEqual(top, ["Top"])
        tail = len(topNodes)
        self.assertEqual(
            [n.getTypeId().getName().getString() for n in nodes[-tail:]],
            [n.getTypeId().getName().getString() for n in topNodes],
        )

    def testChildrenOfTheGroupAreReachedAsBefore(self):
        self.assertEqual(self.path(self.Part, "Group.Box0.")[0], ["Part", "Box0"])
        self.assertEqual(self.path(self.Part, "Box0.")[0], ["Part", "Box0"])
        self.assertEqual(self.path(self.Part, "Out.")[0], ["Part", "Out"])
        self.assertEqual(self.path(self.Part, "")[0], ["Part"])


class PartOpenedCases(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        self.path = os.path.join(tempfile.gettempdir(), "PartGroupViewOpened.FCStd")
        self.Doc = None

    def tearDown(self):
        if self.Doc:
            App.closeDocument(self.Doc.Name)
        if os.path.exists(self.path):
            os.remove(self.path)

    def testOpenedPartIsNotModified(self):
        import FreeCADGui
        from PySide import QtWidgets

        doc = App.newDocument("PartGroupViewOpened")
        part = doc.addObject("App::Part", "Part")
        part.addObject(doc.addObject("Part::Box", "Box"))
        doc.recompute()
        # saved before its origin is sized, so the load has it to size
        doc.saveAs(self.path)
        App.closeDocument(doc.Name)
        self.Doc = doc = App.openDocument(self.path)

        view = FreeCADGui.getDocument(doc.Name)
        origin = [o for o in doc.Objects if o.isDerivedFrom("App::Origin")][0]
        made = tuple(origin.ViewObject.Size)
        self.assertFalse(view.Modified)
        # the sizing waits out the load, 300 ms at a time
        sized = False
        until = time.time() + 5.0
        while time.time() < until and not sized:
            QtWidgets.QApplication.processEvents()
            time.sleep(0.02)
            sized = tuple(origin.ViewObject.Size) != made
        self.assertTrue(sized, "the origin was not sized, so nothing was tested")
        QtWidgets.QApplication.processEvents()
        self.assertFalse(view.Modified)
