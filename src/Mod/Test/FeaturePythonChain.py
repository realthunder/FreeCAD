# SPDX-License-Identifier: LGPL-2.1-or-later
"""The ProxyExp chain: objects extending another object's Proxy hooks.

docs/ProxyChain.md P1.  A FeaturePython asks the objects of its ProxyExp list,
in order and before its own Proxy, for a method named after the hook --
expExecute(obj), expOnChanged(obj, prop), ... -- and the first that answers
stops the chain.  What the linked object IS never enters into it: these cases
use a scripted object's Proxy, a function stored on a plain object, and a
spreadsheet whose alias cells are lambdas, all through the one mechanism.

Headless: nothing here touches a view provider.  The view half is P2.
"""

import os
import tempfile
import unittest

import FreeCAD

# Every extension method appends (tag, hook, detail) here; a case reads it back.
CALLS = []


class LinkProxy:
    """A Proxy that extends OTHER objects rather than its own.

    Resolution path 1 of docs/ProxyChain.md sec 2.2: the linked object carries
    a Proxy, and the chain asks that first.
    """

    def __init__(self, tag="L", handle=True):
        self.tag = tag
        self.handle = handle

    def expExecute(self, obj):
        CALLS.append((self.tag, "expExecute", obj.Name))
        if not self.handle:
            # the protocol's "not mine": the chain moves on
            raise NotImplementedError
        return True

    def expOnChanged(self, obj, prop):
        CALLS.append((self.tag, "expOnChanged", prop))

    def expIsElementVisible(self, obj, element):
        CALLS.append((self.tag, "expIsElementVisible", element))
        if not self.handle:
            return -2
        return 1


class FeatureProxy:
    """The extended object's own Proxy -- the last element of the chain."""

    def __init__(self, tag="P", raising=False):
        self.tag = tag
        self.raising = raising

    def execute(self, obj):
        CALLS.append((self.tag, "execute", obj.Name))
        if self.raising:
            raise RuntimeError("the Proxy should not have been reached")
        return True

    def onChanged(self, obj, prop):
        CALLS.append((self.tag, "onChanged", prop))

    def isElementVisible(self, obj, element):
        CALLS.append((self.tag, "isElementVisible", element))
        return 1


def expExecuteStored(self, obj):
    """Stored on a link object as an attribute, and so bound to it.

    Resolution path 2: no Proxy involved, the callable is an attribute of the
    linked object itself.
    """
    CALLS.append((self.Name, "expExecute", obj.Name))
    return True


def hooks(tag=None, hook=None):
    """The recorded calls, filtered."""
    return [
        call
        for call in CALLS
        if (tag is None or call[0] == tag) and (hook is None or call[1] == hook)
    ]


class FeaturePythonChainCases(unittest.TestCase):
    def setUp(self):
        del CALLS[:]
        self.doc = FreeCAD.newDocument("ProxyChain")
        self.tempdirs = []

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        del CALLS[:]

    def tempfile(self, name):
        directory = tempfile.mkdtemp()
        self.tempdirs.append(directory)
        return os.path.join(directory, name)

    def makeLink(self, name, tag=None, handle=True):
        link = self.doc.addObject("App::FeaturePython", name)
        link.Proxy = LinkProxy(tag or name, handle)
        return link

    def recompute(self, obj):
        obj.touch()
        self.doc.recompute()

    # -- the property itself ------------------------------------------------

    def testPropertyIsThere(self):
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        self.assertIn("ProxyExp", obj.PropertiesList)
        self.assertEqual(obj.ProxyExp, [])
        link = self.makeLink("Link")
        obj.ProxyExp = [link]
        self.assertEqual(obj.ProxyExp, [link])
        # a Global-scope link list is a dependency: the extension recomputes
        # first, and the extended object is in the extension's in-list
        self.assertIn(obj, link.InList)
        self.assertIn(link, obj.OutList)

    def testViewListIsHeadlessAndHidden(self):
        """ViewProxyExp is an App property, so it exists, saves and restores
        with no GUI in sight -- but it is Hidden scope, which is what keeps it
        out of the dependency graph, and it does not touch its object.

        What the linked objects then DO is the view side, docs/ProxyChain.md
        P2, covered by ViewProviderChain under the GUI gate.
        """
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        self.assertIn("ViewProxyExp", obj.PropertiesList)
        self.assertEqual(obj.ViewProxyExp, [])
        link = self.makeLink("Link")
        self.doc.recompute()
        self.assertNotIn("Touched", obj.State)
        obj.ViewProxyExp = [link]
        self.assertNotIn("Touched", obj.State)
        self.assertNotIn(link, obj.OutList)
        self.assertNotIn(obj, link.InList)

        path = self.tempfile("viewchain.FCStd")
        self.doc.saveAs(path)
        FreeCAD.closeDocument(self.doc.Name)
        doc = FreeCAD.openDocument(path)
        self.assertEqual([o.Name for o in doc.getObject("Feature").ViewProxyExp],
                         ["Link"])

    def testEmptyListIsTodaysPath(self):
        """With nothing linked, the object is exactly what it always was."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.Proxy = FeatureProxy()
        link = self.makeLink("Link")
        obj.ProxyExp = [link]
        self.recompute(obj)
        self.assertEqual(hooks("Link", "expExecute"), [("Link", "expExecute", "Feature")])

        del CALLS[:]
        obj.ProxyExp = []
        self.recompute(obj)
        self.assertEqual(hooks("Link"), [])
        self.assertEqual(hooks("P", "execute"), [("P", "execute", "Feature")])

    # -- resolution ---------------------------------------------------------

    def testLinkedProxyHandlesExecute(self):
        """A scripted object's Proxy, called with the extended object."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        link = self.makeLink("Link")
        obj.ProxyExp = [link]
        self.recompute(obj)
        self.assertEqual(hooks("Link", "expExecute"), [("Link", "expExecute", "Feature")])

    def testStoredFunctionHandlesExecute(self):
        """Resolution path 2: an attribute of the linked object itself."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        link = self.doc.addObject("App::FeaturePython", "Link")
        link.expExecute = expExecuteStored
        obj.ProxyExp = [link]
        self.recompute(obj)
        self.assertEqual(hooks("Link", "expExecute"), [("Link", "expExecute", "Feature")])

    def testProxyBeforeOwnAttribute(self):
        """Ruled 2026-09-12: the linked object's Proxy is asked first."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        link = self.makeLink("Link", tag="fromProxy")
        link.expExecute = expExecuteStored
        obj.ProxyExp = [link]
        self.recompute(obj)
        self.assertEqual(hooks(None, "expExecute"), [("fromProxy", "expExecute", "Feature")])

    # -- the walk -----------------------------------------------------------

    def testChainOrderAndFallThrough(self):
        """The first link declines, the second handles, the Proxy is spared."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.Proxy = FeatureProxy(raising=True)
        first = self.makeLink("First", handle=False)
        second = self.makeLink("Second")
        obj.ProxyExp = [first, second]
        self.recompute(obj)
        self.assertEqual(
            hooks(None, "expExecute"),
            [("First", "expExecute", "Feature"), ("Second", "expExecute", "Feature")],
        )
        self.assertEqual(hooks("P", "execute"), [])
        self.assertNotIn("Invalid", obj.State)

    def testProxyIsLast(self):
        """Every link declines, so the object's own Proxy answers."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.Proxy = FeatureProxy()
        link = self.makeLink("Link", handle=False)
        obj.ProxyExp = [link]
        self.recompute(obj)
        self.assertEqual(hooks("Link", "expExecute"), [("Link", "expExecute", "Feature")])
        self.assertEqual(hooks("P", "execute"), [("P", "execute", "Feature")])

    def testValueHookFallsThroughToBase(self):
        """-2, isElementVisible's own "not handled", reaches the C++ base."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        link = self.makeLink("Link", handle=False)
        obj.ProxyExp = [link]
        self.assertEqual(obj.isElementVisible("Edge1"), -1)
        self.assertEqual(hooks("Link", "expIsElementVisible"),
                         [("Link", "expIsElementVisible", "Edge1")])

        del CALLS[:]
        handling = self.makeLink("Handling")
        obj.ProxyExp = [link, handling]
        self.assertEqual(obj.isElementVisible("Edge1"), 1)

    def testNotificationReachesEveryElement(self):
        """onChanged has no result to read, so the whole chain hears it."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.Proxy = FeatureProxy()
        first = self.makeLink("First")
        second = self.makeLink("Second")
        obj.ProxyExp = [first, second]
        obj.addProperty("App::PropertyString", "Note")
        del CALLS[:]
        obj.Note = "hello"
        self.assertEqual(
            [call[0] for call in hooks(None, "expOnChanged") if call[2] == "Note"],
            ["First", "Second"],
        )
        self.assertIn(("P", "onChanged", "Note"), hooks("P", "onChanged"))

    # -- the cache and the generation counter -------------------------------

    def testStoredFunctionReplacedIsPickedUp(self):
        """A definition changed under a feature that did not change."""
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        link = self.doc.addObject("App::FeaturePython", "Link")
        link.expExecute = expExecuteStored
        obj.ProxyExp = [link]
        self.recompute(obj)
        self.assertEqual(len(hooks(None, "expExecute")), 1)

        def replacement(self, obj):
            CALLS.append(("replacement", "expExecute", obj.Name))
            return True

        link.expExecute = replacement
        del CALLS[:]
        self.recompute(obj)
        self.assertEqual(hooks(None, "expExecute"),
                         [("replacement", "expExecute", "Feature")])

    def testLinkedProxyReplacedIsPickedUp(self):
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        link = self.makeLink("Link", tag="before")
        obj.ProxyExp = [link]
        self.recompute(obj)
        self.assertEqual(hooks(None, "expExecute"), [("before", "expExecute", "Feature")])

        link.Proxy = LinkProxy("after")
        del CALLS[:]
        self.recompute(obj)
        self.assertEqual(hooks(None, "expExecute"), [("after", "expExecute", "Feature")])

    def testDeletedLinkDropsOut(self):
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.Proxy = FeatureProxy()
        link = self.makeLink("Link")
        obj.ProxyExp = [link]
        self.doc.removeObject(link.Name)
        del CALLS[:]
        self.recompute(obj)
        self.assertEqual(obj.ProxyExp, [])
        self.assertEqual(hooks("Link"), [])
        self.assertEqual(hooks("P", "execute"), [("P", "execute", "Feature")])

    # -- persistence --------------------------------------------------------

    def testChainSurvivesSaveAndReopen(self):
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        link = self.makeLink("Link")
        obj.ProxyExp = [link]
        path = self.tempfile("chain.FCStd")
        self.doc.saveAs(path)
        FreeCAD.closeDocument(self.doc.Name)

        doc = FreeCAD.openDocument(path)
        del CALLS[:]
        reopened = doc.getObject("Feature")
        self.assertEqual([o.Name for o in reopened.ProxyExp], ["Link"])
        reopened.touch()
        doc.recompute()
        self.assertEqual(hooks("Link", "expExecute"), [("Link", "expExecute", "Feature")])

    def testChainAcrossFiles(self):
        """An XLink: the extension lives in another file and loads on demand."""
        extdoc = FreeCAD.newDocument("ProxyChainExt")
        link = extdoc.addObject("App::FeaturePython", "Link")
        link.Proxy = LinkProxy("External")
        extpath = self.tempfile("extension.FCStd")
        extdoc.saveAs(extpath)

        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.ProxyExp = [link]
        path = self.tempfile("chain.FCStd")
        self.doc.saveAs(path)
        FreeCAD.closeDocument(extdoc.Name)
        FreeCAD.closeDocument(self.doc.Name)

        doc = FreeCAD.openDocument(path)
        del CALLS[:]
        reopened = doc.getObject("Feature")
        self.assertEqual([o.Name for o in reopened.ProxyExp], ["Link"])
        reopened.touch()
        doc.recompute()
        self.assertEqual(hooks("External", "expExecute"),
                         [("External", "expExecute", "Feature")])


class SheetChainCases(unittest.TestCase):
    """The point of the design: a spreadsheet is an extension like any other.

    The sheet's alias'd cells hold lambdas of the expression language, exposed
    as ordinary attributes of the sheet object -- resolution path 2, with no
    Python class anywhere.
    """

    def setUp(self):
        try:
            import Spreadsheet  # noqa: F401
        except ImportError:
            self.skipTest("the Spreadsheet module is not in this build")
        del CALLS[:]
        self.doc = FreeCAD.newDocument("ProxyChainSheet")

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        del CALLS[:]

    def makeSheet(self, cells):
        sheet = self.doc.addObject("Spreadsheet::Sheet", "Sheet")
        for address, (alias, content) in cells.items():
            sheet.set(address, content)
            sheet.setAlias(address, alias)
        self.doc.recompute()
        return sheet

    def testSheetExtendsAValueHook(self):
        sheet = self.makeSheet({"A1": ("expIsElementVisible", "=lambda obj, element: 1")})
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.ProxyExp = [sheet]
        self.assertEqual(obj.isElementVisible("Edge1"), 1)

    def testSheetExtendsExecute(self):
        """The sheet handles execute, so the Proxy that would raise is spared."""
        sheet = self.makeSheet({"A1": ("expExecute", "=lambda obj: 1")})
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.Proxy = FeatureProxy(raising=True)
        obj.ProxyExp = [sheet]
        obj.touch()
        self.doc.recompute()
        self.assertEqual(hooks("P", "execute"), [])
        self.assertNotIn("Invalid", obj.State)

    def testRetypedCellIsPickedUp(self):
        """The generation counter: the cell changes, the feature does not."""
        sheet = self.makeSheet({"A1": ("expIsElementVisible", "=lambda obj, element: 1")})
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.ProxyExp = [sheet]
        self.assertEqual(obj.isElementVisible("Edge1"), 1)

        sheet.set("A1", "=lambda obj, element: 0")
        self.doc.recompute()
        self.assertEqual(obj.isElementVisible("Edge1"), 0)

        # cleared: the sheet no longer extends the hook at all, and the C++
        # base answers again.  Nothing sets a value here -- the cell's property
        # is removed -- so this one rides on PropertySheet::hasSetValue.
        sheet.clear("A1")
        self.doc.recompute()
        self.assertEqual(obj.isElementVisible("Edge1"), -1)
