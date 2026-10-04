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
"""The ViewProxyExp chain: objects extending a view provider's Proxy hooks.

docs/ProxyChain.md P2, the view half of what FeaturePythonChain covers on the
App side.  The list is an ordinary XLinkList on the APP object -- a link
property wants a document object to live in, and one list then serves every
view of the object -- so the view provider reads it through updateData, and
its links are Hidden scope: a reference, not a dependency, which is what lets
an extension read the feature back without closing a cycle.

The method names carry the second prefix of sec 2.2: expViewGetIcon,
expViewClaimChildren, expViewOnChanged.  A chain element is always passed the
view provider, even for the 28 hooks whose Proxy method is passed nothing --
an extension is a separate object serving many features and would otherwise
not know which one it is extending.

Needs the GUI: run it through scripts/sandbox-gui-gate.py.  Skips headless.
"""

import os
import tempfile
import unittest

import FreeCAD

# Every extension method appends (tag, hook, detail) here; a case reads it back.
CALLS = []


class ViewExtension:
    """A Proxy on the LINKED object that extends OTHER objects' view hooks.

    Resolution path 1 of docs/ProxyChain.md sec 2.2, on the view side: the
    linked object carries a Proxy and the chain asks that first.
    """

    def __init__(self, tag="E", handle=True):
        self.tag = tag
        self.handle = handle

    def _decline(self):
        # the protocol's "not mine": the chain moves on to the next element
        raise NotImplementedError

    def expViewGetIcon(self, vobj):
        CALLS.append((self.tag, "expViewGetIcon", vobj.Object.Name))
        if not self.handle:
            self._decline()
        return ":/icons/Tree_Python.svg"

    def expViewClaimChildren(self, vobj):
        CALLS.append((self.tag, "expViewClaimChildren", vobj.Object.Name))
        if not self.handle:
            self._decline()
        return [o for o in vobj.Object.Document.Objects if o.Name.startswith("Child")]

    def expViewGetDropPrefix(self, vobj):
        CALLS.append((self.tag, "expViewGetDropPrefix", vobj.Object.Name))
        if not self.handle:
            self._decline()
        return "from " + self.tag

    def expViewOnChanged(self, vobj, prop):
        CALLS.append((self.tag, "expViewOnChanged", prop))

    def expViewAttach(self, vobj):
        CALLS.append((self.tag, "expViewAttach", vobj.Object.Name))

    def expViewCanReorderObject(self, vobj, obj, before):
        CALLS.append((self.tag, "expViewCanReorderObject", obj.Name))
        if not self.handle:
            self._decline()
        return True


class ViewProxy:
    """The extended object's own view Proxy -- the last element of the chain."""

    def __init__(self, tag="P", raising=False):
        self.tag = tag
        self.raising = raising

    def getIcon(self):
        CALLS.append((self.tag, "getIcon", ""))
        if self.raising:
            raise RuntimeError("the view Proxy should not have been reached")
        return ":/icons/Tree_Python.svg"

    def claimChildren(self):
        CALLS.append((self.tag, "claimChildren", ""))
        return []

    def getDropPrefix(self):
        CALLS.append((self.tag, "getDropPrefix", ""))
        return "from the proxy"

    def onChanged(self, vobj, prop):
        CALLS.append((self.tag, "onChanged", prop))


def hooks(tag=None, hook=None):
    """The recorded calls, filtered."""
    return [
        call
        for call in CALLS
        if (tag is None or call[0] == tag) and (hook is None or call[1] == hook)
    ]


class ViewProviderChainCases(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        del CALLS[:]
        # The extension of a view chain is a document object, and its Proxy
        # restores into the GUEST when routing is on -- these cases read the
        # chain's result on the HOST.  Routing is ON by default since
        # 2026-09-16, so native has to be asked for, and put back.
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Expression/Sandbox")
        had = "Evaluate" in params.GetBools()
        prior = params.GetBool("Evaluate", True)
        params.SetBool("Evaluate", False)
        self.addCleanup(
            lambda: params.SetBool("Evaluate", prior) if had else params.RemBool("Evaluate")
        )
        self.doc = FreeCAD.newDocument("ViewProxyChain")
        self.tempdirs = []

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        del CALLS[:]

    def tempfile(self, name):
        directory = tempfile.mkdtemp()
        self.tempdirs.append(directory)
        return os.path.join(directory, name)

    def reopen(self, path):
        """Open a document and let its view providers be built.

        The fork restores view providers lazily (Document.cpp, the drain), so
        right after openDocument an object's ViewObject is still None; the
        list reaches the provider through its finishRestoring, which runs in
        that pass.
        """
        import FreeCADGui

        doc = FreeCAD.openDocument(path)
        FreeCADGui.updateGui()
        return doc

    def makeExtension(self, name, tag=None, handle=True):
        link = self.doc.addObject("App::FeaturePython", name)
        link.Proxy = ViewExtension(tag or name, handle)
        return link

    def feature(self, name, proxy=None):
        obj = self.doc.addObject("App::FeaturePython", name)
        if proxy is not None:
            obj.ViewObject.Proxy = proxy
        return obj

    # -- the property itself ------------------------------------------------

    def testPropertyIsThere(self):
        obj = self.feature("Feature")
        self.assertIn("ViewProxyExp", obj.PropertiesList)
        self.assertEqual(obj.ViewProxyExp, [])
        ext = self.makeExtension("Ext")
        obj.ViewProxyExp = [ext]
        self.assertEqual(obj.ViewProxyExp, [ext])

    def testViewListIsNotADependency(self):
        """Hidden scope: a reference, so no edge and no recompute order."""
        obj = self.feature("Feature")
        ext = self.makeExtension("Ext")
        obj.ViewProxyExp = [ext]
        self.assertNotIn(ext, obj.OutList)
        self.assertNotIn(obj, ext.InList)

    def testViewListDoesNotTouch(self):
        """Prop_NoRecompute plus the Output status: a view-side edit is not
        a reason to recompute the geometry."""
        obj = self.feature("Feature")
        ext = self.makeExtension("Ext")
        self.doc.recompute()
        self.assertNotIn("Touched", obj.State)
        obj.ViewProxyExp = [ext]
        self.assertNotIn("Touched", obj.State)

    # -- the hooks ----------------------------------------------------------

    def testIconFromExtension(self):
        """A hook whose Proxy method takes nothing still tells the extension
        which view provider it is extending."""
        obj = self.feature("Feature")
        obj.ViewProxyExp = [self.makeExtension("Ext")]
        self.assertIsNotNone(obj.ViewObject.Icon)
        self.assertEqual(hooks("Ext", "expViewGetIcon"),
                         [("Ext", "expViewGetIcon", "Feature")])

    def testClaimChildrenFromExtension(self):
        obj = self.feature("Feature")
        child = self.doc.addObject("App::FeaturePython", "Child")
        obj.ViewProxyExp = [self.makeExtension("Ext")]
        self.assertEqual(obj.ViewObject.claimChildren(), [child])
        # the tree asks this one repeatedly, so only that it was asked
        self.assertIn(("Ext", "expViewClaimChildren", "Feature"),
                      hooks("Ext", "expViewClaimChildren"))

    def testStringHookFromExtension(self):
        """getToolTip, the string hook the plan named, is not reachable from
        Python; getDropPrefix is the same protocol and is."""
        obj = self.feature("Feature")
        obj.ViewProxyExp = [self.makeExtension("Ext")]
        self.assertEqual(obj.ViewObject.DropPrefix, "from Ext")
        self.assertEqual(hooks("Ext", "expViewGetDropPrefix"),
                         [("Ext", "expViewGetDropPrefix", "Feature")])

    def testOnChangedReachesEveryElement(self):
        """A notification has no result to read, so the whole chain hears it."""
        obj = self.feature("Feature", ViewProxy())
        first = self.makeExtension("First")
        second = self.makeExtension("Second")
        obj.ViewProxyExp = [first, second]
        del CALLS[:]
        obj.ViewObject.Visibility = not obj.ViewObject.Visibility
        heard = [c[0] for c in hooks(None, "expViewOnChanged") if c[2] == "Visibility"]
        self.assertEqual(heard, ["First", "Second"])
        self.assertIn(("P", "onChanged", "Visibility"), hooks("P", "onChanged"))

    # -- the walk -----------------------------------------------------------

    def testChainOrderAndFallThrough(self):
        """The first declines, the second answers, the view Proxy is spared."""
        obj = self.feature("Feature", ViewProxy(raising=True))
        first = self.makeExtension("First", handle=False)
        second = self.makeExtension("Second")
        obj.ViewProxyExp = [first, second]
        del CALLS[:]
        self.assertIsNotNone(obj.ViewObject.Icon)
        self.assertEqual([c[0] for c in hooks(None, "expViewGetIcon")],
                         ["First", "Second"])
        self.assertEqual(hooks("P", "getIcon"), [])

    def testViewProxyIsLast(self):
        """Every element declines, so the object's own Proxy answers."""
        obj = self.feature("Feature", ViewProxy())
        obj.ViewProxyExp = [self.makeExtension("Ext", handle=False)]
        del CALLS[:]
        self.assertIsNotNone(obj.ViewObject.Icon)
        self.assertEqual([c[0] for c in hooks(None, "expViewGetIcon")], ["Ext"])
        self.assertEqual(hooks("P", "getIcon"), [("P", "getIcon", "")])

    def testEmptyListIsTodaysPath(self):
        """With nothing linked, the view provider is what it always was."""
        obj = self.feature("Feature", ViewProxy())
        ext = self.makeExtension("Ext")
        obj.ViewProxyExp = [ext]
        del CALLS[:]
        self.assertEqual(obj.ViewObject.DropPrefix, "from Ext")

        obj.ViewProxyExp = []
        del CALLS[:]
        self.assertEqual(obj.ViewObject.DropPrefix, "from the proxy")
        self.assertEqual(hooks("Ext"), [])

    def testAppListDoesNotServeViewHooks(self):
        """The two lists are separate: ProxyExp never answers a view hook."""
        obj = self.feature("Feature", ViewProxy())
        obj.ProxyExp = [self.makeExtension("Ext")]
        del CALLS[:]
        self.assertEqual(obj.ViewObject.DropPrefix, "from the proxy")
        self.assertEqual(hooks("Ext"), [])

    # -- the deferred attach ------------------------------------------------

    def testExtensionAloneAttachesTheViewProvider(self):
        """No view Proxy at all: the list is what supplies the hooks, so it is
        what has to run the attach the template defers."""
        obj = self.feature("Feature")
        obj.ViewProxyExp = [self.makeExtension("Ext")]
        # expViewAttach is the proof: nothing else runs it, and with no Proxy
        # to poke there is nothing else that could have attached the provider
        self.assertEqual(hooks("Ext", "expViewAttach"),
                         [("Ext", "expViewAttach", "Feature")])
        self.assertIsNotNone(obj.ViewObject.RootNode)
        del CALLS[:]
        self.assertEqual(obj.ViewObject.DropPrefix, "from Ext")

    # -- persistence --------------------------------------------------------

    def testListSurvivesSaveAndReopen(self):
        obj = self.feature("Feature")
        obj.ViewProxyExp = [self.makeExtension("Ext")]
        path = self.tempfile("viewchain.FCStd")
        self.doc.saveAs(path)
        FreeCAD.closeDocument(self.doc.Name)

        doc = self.reopen(path)
        reopened = doc.getObject("Feature")
        self.assertEqual([o.Name for o in reopened.ViewProxyExp], ["Ext"])
        # the Proxy of the restored extension is rebuilt by the unpickler
        del CALLS[:]
        self.assertEqual(reopened.ViewObject.DropPrefix, "from Ext")

    def testChainAcrossFiles(self):
        """An XLink: the extension lives in another file and loads on demand."""
        extdoc = FreeCAD.newDocument("ViewProxyChainExt")
        link = extdoc.addObject("App::FeaturePython", "Ext")
        link.Proxy = ViewExtension("External")
        extpath = self.tempfile("extension.FCStd")
        extdoc.saveAs(extpath)

        obj = self.feature("Feature")
        obj.ViewProxyExp = [link]
        path = self.tempfile("viewchain.FCStd")
        self.doc.saveAs(path)
        FreeCAD.closeDocument(extdoc.Name)
        FreeCAD.closeDocument(self.doc.Name)

        doc = self.reopen(path)
        reopened = doc.getObject("Feature")
        self.assertEqual([o.Name for o in reopened.ViewProxyExp], ["Ext"])
        del CALLS[:]
        self.assertEqual(reopened.ViewObject.DropPrefix, "from External")


class SheetViewChainCases(unittest.TestCase):
    """The point of the design, on the view side: a spreadsheet whose alias'd
    cells hold lambdas extends a view provider with no Python class anywhere.
    """

    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        try:
            import Spreadsheet  # noqa: F401
        except ImportError:
            self.skipTest("the Spreadsheet module is not in this build")
        del CALLS[:]
        self.doc = FreeCAD.newDocument("ViewProxyChainSheet")

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        del CALLS[:]

    def makeSheet(self, cells):
        sheet = self.doc.addObject("Spreadsheet::Sheet", "Sheet")
        for address, (alias, content) in cells.items():
            sheet.set(address, content)
            if alias:
                sheet.setAlias(address, alias)
        self.doc.recompute()
        return sheet

    def testSheetExtendsAViewHook(self):
        sheet = self.makeSheet({
            "A1": ("expViewCanReorderObject", "=lambda vobj, obj, before: 1"),
        })
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        other = self.doc.addObject("App::FeaturePython", "Other")
        obj.ViewProxyExp = [sheet]
        self.assertTrue(obj.ViewObject.canReorderObject(other, other))

    def testRetypedCellIsPickedUp(self):
        """The generation counter reaches the view chain too."""
        sheet = self.makeSheet({
            "A1": ("expViewCanReorderObject", "=lambda vobj, obj, before: 1"),
        })
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        other = self.doc.addObject("App::FeaturePython", "Other")
        obj.ViewProxyExp = [sheet]
        self.assertTrue(obj.ViewObject.canReorderObject(other, other))

        sheet.set("A1", "=lambda vobj, obj, before: 0")
        self.doc.recompute()
        self.assertFalse(obj.ViewObject.canReorderObject(other, other))

    def testSheetReadsTheFeatureBack(self):
        """Hidden scope earns its keep: the extension may read what it extends
        without closing a cycle -- which the App list's Global scope would."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.addProperty("App::PropertyFloat", "Size")
        obj.Size = 4.0
        sheet = self.makeSheet({
            "A1": ("expViewCanReorderObject", "=lambda vobj, obj, before: 1"),
            "B1": (None, "=Feature.Size"),
        })
        obj.ViewProxyExp = [sheet]
        self.doc.recompute()
        self.assertEqual(sheet.B1, 4.0)
        self.assertNotIn("Invalid", sheet.State)
        self.assertNotIn("Invalid", obj.State)
        self.assertTrue(obj.ViewObject.canReorderObject(obj, obj))
