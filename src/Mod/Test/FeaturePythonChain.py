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

    def testCellCallsAFunctionCell(self):
        """A cell's function called from another cell, beside the chain."""
        sheet = self.makeSheet({"A1": ("expIsElementVisible", "=lambda obj, element: 1")})
        sheet.set("A2", "=def triple(x): return x * 3\n")
        sheet.set("B1", "=triple(5)")
        sheet.set("B2", "=expIsElementVisible(0, 0) + 1")
        self.doc.recompute()
        self.assertEqual(sheet.B1, 15)
        self.assertEqual(sheet.B2, 2)
        self.assertEqual(repr(sheet.triple), "<Function triple>")


class MarkerProxy:
    """A carrier Proxy whose expExecute writes a value the case can read back."""

    def __init__(self, value):
        self.value = value

    def expExecute(self, obj):
        obj.Marker = self.value
        return True


class OwnProxy:
    """The extended object's own Proxy, reached only when the chain declines."""

    def execute(self, obj):
        obj.Marker = 99
        return True


def storedMarker(value):
    """A plain function to store on a carrier: L.expExecute = storedMarker(n)."""

    def expExecute(self, obj):
        obj.Marker = value
        return True

    return expExecute


def recordingDecline(self, obj):
    """A chain element that records the call and then declines it.

    Returning False is the protocol's "not mine", so whatever stands behind
    this element still runs -- which makes it an execute counter that writes
    nothing.  Nothing is the point: a counter kept on the feature itself
    cannot be used here, because writing a property to record the call is
    itself a touch, and the NEXT recompute would then follow from that rather
    than from what the case is testing.
    """
    CALLS.append((self.Name, "expExecute", obj.Name))
    return False


class ChainRecomputeCases(unittest.TestCase):
    """The recompute of docs/ProxyChain.md sec 4.5: an EDITED method.

    A chain element's definition can change with nothing on the feature
    touched and no revision moving anywhere -- a Spreadsheet::Sheet pins
    getRevision() to 0 on purpose, and a function stored from Python is no
    property at all.  So the recompute decision compares what the chain
    resolves to against what the last run was built against, gated by the
    process-wide generation counter.

    Every case here needs OptimizeRecompute ON, which is its default: with it
    off, Document::_recomputeFeature never reaches the skip these cases are
    about and they all pass for the wrong reason.

    The method cells are written as `=def expExecute(obj): ...` -- a def in a
    cell aliases the cell to the function's own name, so the hook name is
    stated once.  Their inputs come through `obj`, never through the sheet's
    frame, which is the idiom sec 4.5 settles on: a function body's
    identifiers are not dependencies of its cell, so a method reading a
    sibling cell would change behaviour with nothing observable changing.
    """

    PARAMS = "User parameter:BaseApp/Preferences/Document"

    def setUp(self):
        try:
            import Spreadsheet  # noqa: F401
        except ImportError:
            self.skipTest("the Spreadsheet module is not in this build")
        del CALLS[:]
        # forced rather than assumed, and put back exactly as it was found:
        # this one is a PERSISTED user parameter, so a case that left it set
        # would silently change every later run on the box
        params = FreeCAD.ParamGet(self.PARAMS)
        self.hadOptimize = "OptimizeRecompute" in params.GetBools()
        self.oldOptimize = params.GetBool("OptimizeRecompute", True)
        params.SetBool("OptimizeRecompute", True)
        self.doc = FreeCAD.newDocument("ProxyChainRecompute")

    def tearDown(self):
        params = FreeCAD.ParamGet(self.PARAMS)
        if self.hadOptimize:
            params.SetBool("OptimizeRecompute", self.oldOptimize)
        else:
            params.RemBool("OptimizeRecompute")
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        del CALLS[:]

    def makeSheet(self, marker, doc=None):
        """A sheet whose A1 is the execute method and whose B1 is unrelated."""
        doc = doc or self.doc
        sheet = doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "=def expExecute(obj): obj.Marker = %s\n" % marker)
        sheet.set("B1", "=1")
        doc.recompute()
        return sheet

    def makeRecorder(self, doc=None):
        """First in the chain, so every execute of the feature is recorded."""
        doc = doc or self.doc
        recorder = doc.addObject("App::FeaturePython", "Recorder")
        recorder.expExecute = recordingDecline
        return recorder

    def makeFeature(self, carrier, name="Feature", doc=None, recorder=None):
        doc = doc or self.doc
        obj = doc.addObject("App::FeaturePython", name)
        obj.addProperty("App::PropertyInteger", "Marker")
        obj.ProxyExp = [recorder, carrier] if recorder else [carrier]
        doc.recompute()
        return obj

    def executeCount(self, name="Recorder"):
        return len(hooks(name, "expExecute"))

    # 1 -- the method re-typed in its cell, a plain recompute, instances rebuilt

    def testRetypedMethodRebuildsTheInstance(self):
        sheet = self.makeSheet("10")
        obj = self.makeFeature(sheet, recorder=self.makeRecorder())
        self.assertEqual(obj.Marker, 10)
        del CALLS[:]

        sheet.set("A1", "=def expExecute(obj): obj.Marker = 20\n")
        self.doc.recompute()
        self.assertEqual(obj.Marker, 20)
        self.assertEqual(self.executeCount(), 1)

    # 2 -- two instances on one carrier, both rebuilt, each with its own input

    def testBothInstancesRebuildWithTheirOwnInput(self):
        sheet = self.doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "=def expExecute(obj): obj.Marker = obj.Scale * 2\n")
        self.doc.recompute()

        instances = []
        for i, scale in enumerate((3, 5)):
            obj = self.doc.addObject("App::FeaturePython", "Feature%d" % i)
            obj.addProperty("App::PropertyInteger", "Marker")
            obj.addProperty("App::PropertyInteger", "Scale").Scale = scale
            obj.ProxyExp = [sheet]
            instances.append(obj)
        self.doc.recompute()
        self.assertEqual([o.Marker for o in instances], [6, 10])

        sheet.set("A1", "=def expExecute(obj): obj.Marker = obj.Scale * 10\n")
        self.doc.recompute()
        self.assertEqual([o.Marker for o in instances], [30, 50])

    # 3 -- an unrelated cell of the same sheet recomputes no instance

    def testUnrelatedCellRecomputesNoInstance(self):
        sheet = self.makeSheet("10")
        obj = self.makeFeature(sheet, recorder=self.makeRecorder())
        self.assertEqual(obj.Marker, 10)
        del CALLS[:]

        # the method cell is not dirty, so Sheet::execute does not re-evaluate
        # it and it still holds the SAME callable -- which is what the
        # comparison sees, and why this costs nothing
        sheet.set("B1", "=2")
        self.doc.recompute()
        self.assertEqual(self.executeCount(), 0)

        # and the negative is not vacuous: the counter DID move on that edit
        # -- PropertySheet::hasSetValue bumps it for any cell -- so the zero
        # above is the identity comparison answering, not a path never taken.
        # The same recompute with the method cell dirty rebuilds.
        sheet.set("A1", "=def expExecute(obj): obj.Marker = 11\n")
        self.doc.recompute()
        self.assertEqual(self.executeCount(), 1)
        self.assertEqual(obj.Marker, 11)

    # 4 -- the plain-spreadsheet regression, no ProxyExp anywhere

    def testPlainSheetConsumerFollowsItsOwnCellOnly(self):
        """The property-level dependency Sheet::getRevision()==0 protects.

        Not about the chain at all: it is what a coarse fix -- a real sheet
        revision, or touching the carrier's InList -- would have broken.
        """
        sheet = self.doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "=2")
        sheet.setAlias("A1", "mine")
        sheet.set("B1", "=7")
        sheet.setAlias("B1", "theirs")
        self.doc.recompute()

        obj = self.doc.addObject("App::FeaturePython", "Consumer")
        obj.addProperty("App::PropertyInteger", "Marker")
        obj.setExpression("Marker", "Sheet.mine")
        obj.Proxy = FeatureProxy()
        self.doc.recompute()
        self.assertEqual(obj.Marker, 2)
        del CALLS[:]

        # the cell it does NOT read moves: nothing about this object changed
        sheet.set("B1", "=8")
        self.doc.recompute()
        self.assertEqual(hooks("P", "execute"), [])

        # the cell it does read moves: it follows
        sheet.set("A1", "=3")
        self.doc.recompute()
        self.assertEqual(obj.Marker, 3)
        self.assertEqual(len(hooks("P", "execute")), 1)

    # 5 -- the two non-sheet definition changes

    def testCarrierProxyReplacedRebuilds(self):
        carrier = self.doc.addObject("App::FeaturePython", "Carrier")
        carrier.Proxy = MarkerProxy(1)
        obj = self.makeFeature(carrier)
        self.assertEqual(obj.Marker, 1)

        # a property change on the carrier: its revision moves and the link
        # reports itself touched, so this one never needed the comparison
        carrier.Proxy = MarkerProxy(2)
        self.doc.recompute()
        self.assertEqual(obj.Marker, 2)

    def testStoredFunctionReplacedRebuilds(self):
        carrier = self.doc.addObject("App::FeaturePython", "Carrier")
        carrier.expExecute = storedMarker(1)
        obj = self.makeFeature(carrier)
        self.assertEqual(obj.Marker, 1)

        # no property, no revision, nothing touched anywhere: the comparison
        # is the only thing that can see this
        carrier.expExecute = storedMarker(2)
        self.doc.recompute()
        self.assertEqual(obj.Marker, 2)

    # 6 -- a view hook re-typed on the same sheet rebuilds no geometry

    def testRetypedViewHookRebuildsNothing(self):
        sheet = self.makeSheet("10")
        sheet.set("A2", "=def expViewGetIcon(vobj): 1\n")
        self.doc.recompute()
        obj = self.makeFeature(sheet, recorder=self.makeRecorder())
        self.assertEqual(obj.Marker, 10)
        del CALLS[:]

        # the same sheet, a definition that really did move -- but not this
        # hook's.  Only execute is watched.
        sheet.set("A2", "=def expViewGetIcon(vobj): 2\n")
        self.doc.recompute()
        self.assertEqual(self.executeCount(), 0)

    # 7 -- the method cell cleared: the chain shortens, once

    def testClearedMethodCellShortensTheChain(self):
        sheet = self.makeSheet("10")
        recorder = self.makeRecorder()
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.addProperty("App::PropertyInteger", "Marker")
        obj.Proxy = OwnProxy()
        obj.ProxyExp = [recorder, sheet]
        self.doc.recompute()
        # the sheet handled it and the Proxy behind it was never reached
        self.assertEqual(obj.Marker, 10)
        del CALLS[:]

        # the cell is gone, so the dynamic property behind the alias is
        # REMOVED: the chain is one element shorter and the Proxy answers
        sheet.clear("A1")
        self.doc.recompute()
        self.assertEqual(obj.Marker, 99)
        self.assertEqual(self.executeCount(), 1)

        # and it settles: nothing keeps recomputing on the shortened chain
        del CALLS[:]
        self.doc.recompute()
        self.assertEqual(self.executeCount(), 0)

    # 8 -- an empty ProxyExp is the P0 path, and stays out of all of this

    def testEmptyListIsNotRecomputedByAGenerationBump(self):
        obj = self.doc.addObject("App::FeaturePython", "Feature")
        obj.Proxy = FeatureProxy()
        self.doc.recompute()
        self.assertEqual(obj.ProxyExp, [])
        del CALLS[:]

        # a Proxy assigned anywhere in the process bumps the counter; an
        # object with no chain must not even look
        other = self.doc.addObject("App::FeaturePython", "Other")
        other.Proxy = MarkerProxy(1)
        self.doc.recompute()
        self.assertEqual(hooks("P", "execute"), [])


# -- P3: the same chain with every evaluation in the sandbox guest --------

SANDBOX = "User parameter:BaseApp/Preferences/Expression/Sandbox"
SECURITY = "User parameter:BaseApp/Preferences/Expression/Security"


def requireGuest(case):
    """Skip before a case sets anything: a skip raised in setUp runs no tearDown."""
    if not FreeCAD.ExpressionSandbox.available():
        case.skipTest("no sandbox guest in this build")


def routeEvaluations(case):
    """Every evaluation of the case crosses into the guest.

    The preference is PERSISTED, so it is removed again rather than set back
    to False, exactly as the gtests leave it.
    """
    FreeCAD.ExpressionSandbox.setRouting(True)
    case.addCleanup(lambda: FreeCAD.ParamGet(SANDBOX).RemBool("Evaluate"))


class RoutedSheetChainCases(SheetChainCases):
    """SheetChainCases routed: a cell's function is a guest function.

    docs/ProxyChain.md P3.  A function cannot leave its routed evaluation, so
    the cell holds a host stand-in and a call is an evaluation of its own.
    """

    def setUp(self):
        requireGuest(self)
        super().setUp()
        routeEvaluations(self)


class RoutedChainRecomputeCases(ChainRecomputeCases):
    """ChainRecomputeCases routed: the edited method, and the unrelated cell.

    Routed, a re-evaluated cell holds a NEW stand-in and an untouched one
    keeps the one it has -- the identity the comparison of sec 4.5 reads.
    """

    def setUp(self):
        requireGuest(self)
        super().setUp()
        routeEvaluations(self)


FLANGE = (
    "=def expExecute(obj):\n"
    "    import Part\n"
    "    body = Part.makeCylinder(obj.Dia / 2, obj.Thick)\n"
    "    body = body.cut(Part.makeCylinder(obj.Bore / 2, obj.Thick * 3,"
    " vector(0, 0, -obj.Thick)))\n"
    "    i = 0\n"
    "    while i < obj.Bolts:\n"
    "        a = i * 360deg / obj.Bolts\n"
    "        body = body.cut(Part.makeCylinder(obj.HoleDia / 2, obj.Thick * 3,"
    " vector(obj.Pcd / 2 * cos(a), obj.Pcd / 2 * sin(a), -obj.Thick)))\n"
    "        i = i + 1\n"
    "    obj.Shape = body\n"
    "    return True\n"
)


class RoutedChainCases(unittest.TestCase):
    """What only the routed chain has: the stand-in, and whose file a call runs as.

    RULED 2026-09-13, "Run as the feature's file": a chain method runs under
    the principal of the FEATURE's document with the feature as its write
    owner, so a method linked from another file writing `obj` is a same-file
    write with that file's grants.  Only the chain's call is bound so; the
    same function called any other way runs as its own file.
    """

    def setUp(self):
        requireGuest(self)
        try:
            import Spreadsheet  # noqa: F401
        except ImportError:
            self.skipTest("the Spreadsheet module is not in this build")
        self.doc = FreeCAD.newDocument("ProxyChainRouted")
        self.tempdirs = []
        routeEvaluations(self)

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        for directory in self.tempdirs:
            for entry in os.listdir(directory):
                os.remove(os.path.join(directory, entry))
            os.rmdir(directory)

    def tempfile(self, name):
        directory = tempfile.mkdtemp()
        self.tempdirs.append(directory)
        return os.path.join(directory, name)

    def makeFeature(self, carrier, doc=None, type="App::FeaturePython"):
        doc = doc or self.doc
        obj = doc.addObject(type, "Feature")
        obj.addProperty("App::PropertyInteger", "Marker")
        obj.addProperty("App::PropertyInteger", "Scale").Scale = 3
        obj.ProxyExp = [carrier]
        return obj

    def testMethodIsAStandInAndTheCallCrosses(self):
        sheet = self.doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "=def expExecute(obj): obj.Marker = obj.Scale * 2\n")
        self.doc.recompute()
        self.assertEqual(repr(sheet.expExecute), "<Function expExecute>")
        self.assertEqual(type(sheet.expExecute).__name__, "RoutedFunction")

        obj = self.makeFeature(sheet)
        before = FreeCAD.ExpressionSandbox.evalCount()
        self.doc.recompute()
        self.assertEqual(obj.Marker, 6)
        self.assertNotIn("Invalid", obj.State)
        self.assertGreater(FreeCAD.ExpressionSandbox.evalCount(), before)

    def testMethodFromAnotherFileRunsAsTheFeaturesFile(self):
        extdoc = FreeCAD.newDocument("ProxyChainRoutedExt")
        sheet = extdoc.addObject("Spreadsheet::Sheet", "Type")
        sheet.set("A1", "=def expExecute(obj): obj.Marker = obj.Scale * 2\n")
        extdoc.recompute()
        extdoc.saveAs(self.tempfile("type.FCStd"))

        obj = self.makeFeature(sheet)
        self.doc.saveAs(self.tempfile("instance.FCStd"))
        self.doc.recompute()
        # a write to another file's object would be refused; this one is the
        # feature's own, under the feature's document
        self.assertEqual(obj.Marker, 6)
        self.assertNotIn("Invalid", obj.State)

        # the same function NOT called by the chain runs as its own file,
        # where the feature is another document's object
        with self.assertRaises(PermissionError):
            sheet.expExecute(obj)

    def testFlangeRoutedMatchesNative(self):
        """The sheet flange of docs/Sandbox.md 7.17: routed = native, BRep and all."""
        try:
            import Part  # noqa: F401
        except ImportError:
            self.skipTest("the Part module is not in this build")

        def build():
            doc = FreeCAD.newDocument("Flange")
            sheet = doc.addObject("Spreadsheet::Sheet", "Type")
            sheet.set("A1", FLANGE)
            obj = doc.addObject("Part::FeaturePython", "Flange")
            for name, value in (("Dia", 60), ("Thick", 8), ("Bore", 20), ("Pcd", 44),
                                ("HoleDia", 6)):
                obj.addProperty("App::PropertyLength", name)
                setattr(obj, name, value)
            obj.addProperty("App::PropertyInteger", "Bolts").Bolts = 6
            obj.ProxyExp = [sheet]
            doc.recompute()
            self.assertNotIn("Invalid", obj.State)
            shape = obj.Shape
            FreeCAD.closeDocument(doc.Name)
            return shape

        routed = build()

        # the native twin runs with enforcement off, as the corpus rig does:
        # natively `import Part` is host.import, PROMPT for a document
        FreeCAD.ParamGet(SANDBOX).RemBool("Evaluate")
        params = FreeCAD.ParamGet(SECURITY)
        hadEnforce = "Enforce" in params.GetBools()
        oldEnforce = params.GetBool("Enforce", True)
        params.SetBool("Enforce", False)
        try:
            native = build()
        finally:
            if hadEnforce:
                params.SetBool("Enforce", oldEnforce)
            else:
                params.RemBool("Enforce")

        self.assertEqual(len(routed.Faces), 10)
        self.assertAlmostEqual(routed.Volume, native.Volume, places=6)
        self.assertEqual(routed.exportBrepToString(), native.exportBrepToString())
