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
"""Which view hook a ViewProviderFeaturePython calls, and with what.

The Python and C++ suites are both headless, so nothing in either one
reaches a view provider: every hook in ViewProviderFeaturePythonImp is
uncovered by them. This module is that cover. It drives a view Proxy
that records each call and its argument shape, which is what the hook
table of docs/ProxyChain.md sec 3 states per hook -- whether the owner
is passed at all, and which object it is.

It also pins the bug this module was written for: the template's
canReorderObject asked the Proxy's canReplaceObject, from the commit
that added reordering (638f3bbf20, 2021-12-05) until 2026-09-12. A
scripted view provider's canReorderObject was never called, and a
scripted canReplaceObject silently answered reorder questions instead.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless.
"""

import unittest

import FreeCAD


class Recorder:
    """A view Proxy that records every hook it is asked, with the type
    name of each argument, and answers whatever the test set."""

    def __init__(self, vobj, answers=None, hooks=()):
        self.calls = []
        self.answers = answers or {}
        # only the hooks the test names are exposed, so a case can ask
        # what happens when one is absent
        for name in hooks:
            setattr(self, name, self._make(name))
        vobj.Proxy = self

    def _make(self, name):
        def hook(*args):
            self.calls.append((name, [type(a).__name__ for a in args]))
            return self.answers.get(name)

        return hook

    def names(self):
        return [c[0] for c in self.calls]

    def args(self, name):
        for called, shape in self.calls:
            if called == name:
                return shape
        return None


class ViewProviderHooksTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        self.doc = FreeCAD.newDocument("ViewProviderHooks")

    def tearDown(self):
        if getattr(self, "doc", None):
            FreeCAD.closeDocument(self.doc.Name)
            self.doc = None

    def feature(self, name, answers=None, hooks=()):
        obj = self.doc.addObject("App::FeaturePython", name)
        rec = Recorder(obj.ViewObject, answers, hooks)
        return obj, rec

    # -- the reorder query ------------------------------------------------

    def testCanReorderObjectIsAsked(self):
        """canReorderObject reaches the Proxy, and its answer is the answer."""
        obj, rec = self.feature("Parent", {"canReorderObject": True},
                                ["canReorderObject"])
        other = self.doc.addObject("App::FeaturePython", "Other")
        self.assertTrue(obj.ViewObject.canReorderObject(other, other))
        self.assertIn("canReorderObject", rec.names())

        rec.calls = []
        rec.answers["canReorderObject"] = False
        self.assertFalse(obj.ViewObject.canReorderObject(other, other))
        self.assertIn("canReorderObject", rec.names())

    def testCanReplaceObjectDoesNotAnswerReorder(self):
        """A Proxy with only canReplaceObject is not consulted for a reorder.

        The regression guard: before the fix the template asked
        canReplaceObject here, so this Proxy would have answered True.
        """
        obj, rec = self.feature("Parent", {"canReplaceObject": True},
                                ["canReplaceObject"])
        other = self.doc.addObject("App::FeaturePython", "Other")
        self.assertFalse(obj.ViewObject.canReorderObject(other, other))
        self.assertEqual(rec.names(), [])

    def testCanReplaceObjectStillAnswersReplace(self):
        """...and the replace query still reaches it."""
        obj, rec = self.feature("Parent", {"canReplaceObject": True},
                                ["canReplaceObject"])
        other = self.doc.addObject("App::FeaturePython", "Other")
        self.assertTrue(obj.ViewObject.canReplaceObject(other, other))
        self.assertIn("canReplaceObject", rec.names())

    # -- the three forms of the owner argument ----------------------------

    def testOwnerIsNotPassedToGetIcon(self):
        """Most view hooks pass no owner: the Proxy's own self is it."""
        obj, rec = self.feature("Feat", {"getIcon": ":/icons/Tree_Python.svg"},
                                ["getIcon"])
        self.assertIsNotNone(obj.ViewObject.Icon)
        self.assertEqual(rec.args("getIcon"), [])

    def testOwnerIsPassedToDragObject(self):
        """A Modern hook takes the view provider first."""
        obj, rec = self.feature("Feat", {}, ["dragObject"])
        other = self.doc.addObject("App::FeaturePython", "Other")
        obj.ViewObject.dragObject(other)
        self.assertEqual(rec.args("dragObject"),
                         ["ViewProviderDocumentObject", "FeaturePython"])

    def testOwnerIsPassedToDropObjectEx(self):
        """An Always hook takes it too, ahead of its own arguments."""
        obj, rec = self.feature("Feat", {"dropObjectEx": ""}, ["dropObjectEx"])
        other = self.doc.addObject("App::FeaturePython", "Other")
        obj.ViewObject.dropObject(other, None, "", [])
        self.assertEqual(rec.args("dropObjectEx"),
                         ["ViewProviderDocumentObject", "FeaturePython",
                          "NoneType", "str", "tuple"])

    def testUpdateDataIsPassedTheDocumentObject(self):
        """updateData is the one view hook whose owner is the App object."""
        obj, rec = self.feature("Feat", {}, ["updateData"])
        obj.addProperty("App::PropertyFloat", "Size")
        rec.calls = []
        obj.Size = 4.0
        self.doc.recompute()
        self.assertEqual(rec.args("updateData"), ["FeaturePython", "str"])

    def testOnChangedIsPassedTheViewProvider(self):
        """Its view-side namesake takes the view provider, not the object."""
        obj, rec = self.feature("Feat", {}, ["onChanged"])
        rec.calls = []
        obj.ViewObject.Visibility = not obj.ViewObject.Visibility
        self.assertEqual(rec.args("onChanged"),
                         ["ViewProviderDocumentObject", "str"])
