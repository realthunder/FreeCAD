# SPDX-License-Identifier: LGPL-2.1-or-later
"""Expression libraries: a document's code, imported by its expressions.

docs/Sandbox.md 7.17 (c), the library half of the SandboxProgram gate (D2).
An App::ExpressionLibrary holds engine statements.  `import <Module>` in an
expression of the same document runs them once and binds what they left as a
module, whose functions resolve the module's names -- not their caller's.
Importing a library is a dependency on it.

These cases run natively.  The routed twin of each is the gtest
ExpressionRoutingTest.libraries*, which needs the guest.
"""

import os
import shutil
import tempfile
import unittest

import FreeCAD

BRACKETS = (
    "k = 3\n"
    "def triple(x):\n"
    "    return x * k\n"
    "def viaLater(x):\n"
    "    return later(x) + 1\n"
    "def later(x):\n"
    "    return x * 2\n"
    "def leak():\n"
    "    return secret\n"
)


class SandboxProgramLibraryCases(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("SandboxProgram")
        self.tempdirs = []

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        for directory in self.tempdirs:
            shutil.rmtree(directory, ignore_errors=True)

    def makeLibrary(self, doc=None, text=BRACKETS, module="brackets", name="Lib"):
        doc = doc or self.doc
        lib = doc.addObject("App::ExpressionLibrary", name)
        lib.Module = module
        lib.Text = text
        return lib

    def makeConsumer(self, expression, doc=None, name="Consumer"):
        doc = doc or self.doc
        obj = doc.addObject("App::FeaturePython", name)
        obj.addProperty("App::PropertyFloat", "Out")
        obj.setExpression("Out", expression)
        return obj

    # -- the module --------------------------------------------------------

    def testImport(self):
        self.makeLibrary()
        obj = self.makeConsumer("import brackets\nbrackets.triple(2)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)

    def testFromImport(self):
        self.makeLibrary()
        obj = self.makeConsumer("from brackets import triple\ntriple(5)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 15.0)

    def testModuleNameDefaultsToObjectName(self):
        # lower-cased: `Shapes.triple` would parse as a property of the object
        lib = self.doc.addObject("App::ExpressionLibrary", "Shapes")
        lib.Text = BRACKETS
        self.assertEqual(FreeCAD.ExpressionSandbox.libraries(self.doc), {"shapes": lib})
        obj = self.makeConsumer("import shapes\nshapes.triple(1)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 3.0)

    def testFunctionResolvesItsModule(self):
        # viaLater calls a function defined AFTER it in the text, and triple
        # reads the module's k: both are the module's names, seen at call time
        self.makeLibrary()
        obj = self.makeConsumer("import brackets\nbrackets.viaLater(3)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 7.0)

    def testFunctionDoesNotSeeItsCaller(self):
        # an engine function without a module resolves free names through its
        # caller's frames; a library's must not, or the module means something
        # else in every consumer
        self.makeLibrary()
        obj = self.makeConsumer("secret = 5\nimport brackets\nbrackets.leak()")
        self.doc.recompute()
        self.assertIn("Invalid", obj.State)

    def testBlankLinesAndTrailingComments(self):
        # a comment AFTER code is one: the lexer eats '# text' up to the newline
        text = "\n" "hole_d = 4  # sizes\n" "\n" "def hole(n):\n" "    return n * hole_d\n" "\n"
        self.makeLibrary(text=text)
        obj = self.makeConsumer("import brackets\nbrackets.hole(2) + brackets.hole_d")
        self.doc.recompute()
        self.assertEqual(obj.Out, 12.0)

    def testCommentLineIsASyntaxError(self):
        # ... but a line holding ONLY a comment leaves a bare NEWLINE where a
        # statement must start (ExpressionParser.l, outside python mode): the
        # library is refused, and its consumer waits
        lib = self.makeLibrary(text="# sizes\nhole_d = 4\n")
        obj = self.makeConsumer("import brackets\nbrackets.hole_d")
        self.doc.recompute()
        self.assertIn("Invalid", lib.State)
        self.assertNotEqual(obj.Out, 4.0)

    def testLibraryImportsLibrary(self):
        self.makeLibrary()
        self.makeLibrary(
            text="import brackets\ndef sixfold(x):\n    return brackets.triple(x) * 2\n",
            module="more",
            name="More",
        )
        obj = self.makeConsumer("import more\nmore.sixfold(1)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)

    def testEditImportedLibraryRecomputes(self):
        # a consumer of `more` depends on what `more` imports, and `more`'s
        # module rebuilds when `brackets` does rather than keep the old one
        lib = self.makeLibrary()
        self.makeLibrary(
            text="import brackets\ndef sixfold(x):\n    return brackets.triple(x) * 2\n",
            module="more",
            name="More",
        )
        obj = self.makeConsumer("import more\nmore.sixfold(1)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)
        self.assertIn(lib, obj.OutList)
        lib.Text = BRACKETS.replace("k = 3", "k = 4")
        self.doc.recompute()
        self.assertEqual(obj.Out, 8.0)

    def testCircularImport(self):
        self.makeLibrary(text="import brackets\nk = 1\n")
        obj = self.makeConsumer("import brackets\nbrackets.k")
        self.doc.recompute()
        self.assertIn("Invalid", obj.State)

    def testSyntaxErrorShowsOnTheLibrary(self):
        lib = self.makeLibrary(text="def (:\n")
        self.doc.recompute()
        self.assertIn("Invalid", lib.State)

    def testTwoDocumentsKeepTheirOwnModule(self):
        self.makeLibrary()
        first = self.makeConsumer("import brackets\nbrackets.triple(2)")
        other = FreeCAD.newDocument("SandboxProgramOther")
        self.makeLibrary(doc=other, text=BRACKETS.replace("k = 3", "k = 7"))
        second = self.makeConsumer("import brackets\nbrackets.triple(2)", doc=other)
        self.doc.recompute()
        other.recompute()
        self.assertEqual(first.Out, 6.0)
        self.assertEqual(second.Out, 14.0)

    # -- the dependency ------------------------------------------------------

    def testImportIsADependency(self):
        lib = self.makeLibrary()
        obj = self.makeConsumer("import brackets\nbrackets.triple(2)")
        self.assertIn(lib, obj.OutList)
        plain = self.makeConsumer("import math\nmath.sqrt(4)", name="Plain")
        self.assertEqual(plain.OutList, [])

    def testTextEditRecomputes(self):
        lib = self.makeLibrary()
        obj = self.makeConsumer("from brackets import triple\ntriple(2)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)
        lib.Text = BRACKETS.replace("k = 3", "k = 4")
        self.doc.recompute()
        self.assertEqual(obj.Out, 8.0)

    def testRenameRecomputesAndFails(self):
        lib = self.makeLibrary()
        obj = self.makeConsumer("import brackets\nbrackets.triple(2)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)
        lib.Module = "renamed"
        self.doc.recompute()
        self.assertIn("Invalid", obj.State)

    def testSaveAndReopen(self):
        self.makeLibrary()
        self.makeConsumer("import brackets\nbrackets.triple(4)")
        self.doc.recompute()
        directory = tempfile.mkdtemp()
        self.tempdirs.append(directory)
        path = os.path.join(directory, "library.FCStd")
        self.doc.saveAs(path)
        FreeCAD.closeDocument(self.doc.Name)
        self.doc = FreeCAD.openDocument(path)
        lib = self.doc.getObject("Lib")
        self.assertEqual(lib.TypeId, "App::ExpressionLibrary")
        self.assertEqual(lib.Module, "brackets")
        lib.Text = BRACKETS.replace("k = 3", "k = 5")
        self.doc.recompute()
        self.assertEqual(self.doc.getObject("Consumer").Out, 20.0)

    # -- what the sandbox keeps ----------------------------------------------

    def testLibrariesListing(self):
        lib = self.makeLibrary()
        self.assertEqual(FreeCAD.ExpressionSandbox.libraries(self.doc), {"brackets": lib})

    def testTextIsTheDocumentsCode(self):
        # the principal hashes the document's code; a library is code
        lib = self.makeLibrary()
        before = FreeCAD.ExpressionSecurity.principalOf(self.doc.Name)
        lib.Text = BRACKETS.replace("k = 3", "k = 9")
        self.assertNotEqual(FreeCAD.ExpressionSecurity.principalOf(self.doc.Name), before)

    def testSurfaceRecordedAtEdit(self):
        version = FreeCAD.ExpressionSandbox.surfaceVersion()["version"]
        if not version:
            self.skipTest("this build has no sandbox surface")
        lib = self.makeLibrary()
        self.assertEqual(lib.Surface, str(version))


class SandboxProgramLinkedCases(unittest.TestCase):
    """A library in one file, its consumers in another: Source, Pinned, Snapshot.

    docs/Sandbox.md 7.17 (c), "Across files".  A linked library takes its
    text from the library its Source reaches, under a module name of its
    own; live it is the source's module, pinned it is the snapshot taken at
    the pin, which serves with the source file gone.
    """

    def setUp(self):
        self.directory = tempfile.mkdtemp()
        self.source = FreeCAD.newDocument("LibSource")
        self.lib = self.source.addObject("App::ExpressionLibrary", "Lib")
        self.lib.Module = "brackets"
        self.lib.Text = BRACKETS
        self.source.recompute()
        self.sourcePath = os.path.join(self.directory, "source.FCStd")
        self.source.saveAs(self.sourcePath)
        self.doc = FreeCAD.newDocument("LibConsumer")

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        shutil.rmtree(self.directory, ignore_errors=True)

    def link(self, module="brk", pinned=False, name="LibB"):
        lib = self.doc.addObject("App::ExpressionLibrary", name)
        lib.Module = module
        lib.Source = self.lib
        if pinned:
            lib.Pinned = True
        return lib

    def consumer(self, expression, name="Consumer"):
        obj = self.doc.addObject("App::FeaturePython", name)
        obj.addProperty("App::PropertyFloat", "Out")
        obj.setExpression("Out", expression)
        return obj

    def saveConsumerAndCloseAll(self):
        path = os.path.join(self.directory, "consumer.FCStd")
        self.doc.saveAs(path)
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        return path

    # -- live ----------------------------------------------------------------

    def testLiveLinkImports(self):
        link = self.link()
        obj = self.consumer("import brk\nbrk.triple(2)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)
        self.assertIn(link, obj.OutList)
        self.assertEqual(FreeCAD.ExpressionSandbox.libraries(self.doc), {"brk": link})

    def testLiveLinkFollowsAnEdit(self):
        # the source's edit reaches the consumer whether or not the source's
        # document recomputed first
        self.link()
        obj = self.consumer("from brk import triple\ntriple(2)")
        self.doc.recompute()
        self.lib.Text = BRACKETS.replace("k = 3", "k = 4")
        self.doc.recompute()
        self.assertEqual(obj.Out, 8.0)
        self.lib.Text = BRACKETS.replace("k = 3", "k = 5")
        self.source.recompute()
        self.doc.recompute()
        self.assertEqual(obj.Out, 10.0)

    def testModuleNameIsTheConsumers(self):
        # the relabel is local: the source renaming its own module changes
        # nothing for a consumer that named the link
        self.link(module="mine")
        obj = self.consumer("import mine\nmine.triple(1)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 3.0)
        self.lib.Module = "renamed"
        self.source.recompute()
        self.doc.recompute()
        self.assertEqual(obj.Out, 3.0)
        self.assertNotIn("Invalid", obj.State)

    def testLinkedLibraryImportsInItsOwnFile(self):
        # the source's text imports a library of the SOURCE's document, which
        # the consumer's document does not hold; an edit there follows too
        helper = self.source.addObject("App::ExpressionLibrary", "Helper")
        helper.Module = "helpers"
        helper.Text = "def double(x):\n    return x * 2\n"
        self.lib.Text = (
            BRACKETS + "import helpers\ndef quad(x):\n    return helpers.double(x) * 2\n"
        )
        self.source.recompute()
        self.link()
        obj = self.consumer("import brk\nbrk.quad(3)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 12.0)
        helper.Text = "def double(x):\n    return x * 3\n"
        self.doc.recompute()
        self.assertEqual(obj.Out, 18.0)

    def testTwoLinksShareOneModule(self):
        # ten consumers of one source build the module once
        first = self.link()
        second = self.link(module="other", name="LibC")
        obj = self.consumer("import brk\nimport other\nbrk.triple(1) + other.triple(1)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)
        del first, second

    def testSourceThatIsNoLibrary(self):
        plain = self.source.addObject("App::FeaturePython", "Plain")
        link = self.doc.addObject("App::ExpressionLibrary", "LibB")
        link.Module = "brk"
        link.Source = plain
        obj = self.consumer("import brk\nbrk.triple(2)")
        self.doc.recompute()
        self.assertIn("Invalid", link.State)
        # a consumer of a failed library waits, as for a syntax error
        self.assertIn("Touched", obj.State)
        self.assertNotEqual(obj.Out, 6.0)

    def testReopenLoadsTheSource(self):
        self.link()
        self.consumer("import brk\nbrk.triple(4)")
        self.doc.recompute()
        path = self.saveConsumerAndCloseAll()
        self.doc = FreeCAD.openDocument(path)
        # a reopened file's document is named after the file
        loaded = [
            d
            for d in FreeCAD.listDocuments().values()
            if os.path.normcase(d.FileName) == os.path.normcase(self.sourcePath)
        ]
        self.assertEqual(len(loaded), 1, "the link did not load the source file")
        self.source = loaded[0]
        obj = self.doc.getObject("Consumer")
        self.source.getObject("Lib").Text = BRACKETS.replace("k = 3", "k = 5")
        self.doc.recompute()
        self.assertEqual(obj.Out, 20.0)

    def testLiveWithoutTheSourceFails(self):
        self.link()
        self.consumer("import brk\nbrk.triple(4)")
        self.doc.recompute()
        path = self.saveConsumerAndCloseAll()
        os.remove(self.sourcePath)
        self.doc = FreeCAD.openDocument(path)
        obj = self.doc.getObject("Consumer")
        obj.setExpression("Out", "import brk\nbrk.triple(5)")
        self.doc.recompute()
        self.assertIn("Invalid", self.doc.getObject("LibB").State)
        # a consumer of a failed library waits, as for a syntax error
        self.assertIn("Touched", obj.State)
        self.assertNotEqual(obj.Out, 15.0)

    # -- pinned --------------------------------------------------------------

    def testPinTakesTheSnapshot(self):
        link = self.link(pinned=True)
        self.assertEqual(link.Snapshot, BRACKETS)
        self.assertEqual(link.Surface, self.lib.Surface)
        obj = self.consumer("import brk\nbrk.triple(2)")
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)
        self.lib.Text = BRACKETS.replace("k = 3", "k = 4")
        self.source.recompute()
        self.doc.recompute()
        self.assertEqual(obj.Out, 6.0)

    def testUnpinFollowsTheSourceAgain(self):
        link = self.link(pinned=True)
        obj = self.consumer("import brk\nbrk.triple(2)")
        self.doc.recompute()
        self.lib.Text = BRACKETS.replace("k = 3", "k = 4")
        self.source.recompute()
        link.Pinned = False
        self.assertEqual(link.Snapshot, "")
        self.doc.recompute()
        self.assertEqual(obj.Out, 8.0)

    def testPinnedOpensWithoutTheSource(self):
        self.link(pinned=True)
        self.consumer("import brk\nbrk.triple(4)")
        self.doc.recompute()
        path = self.saveConsumerAndCloseAll()
        os.remove(self.sourcePath)
        self.doc = FreeCAD.openDocument(path)
        obj = self.doc.getObject("Consumer")
        obj.setExpression("Out", "import brk\nbrk.triple(5)")
        self.doc.recompute()
        self.assertNotIn("Invalid", obj.State)
        self.assertEqual(obj.Out, 15.0)

    # -- the principal -------------------------------------------------------

    def testPrincipalHoldsTheLinkNotTheText(self):
        link = self.link()
        principalOf = FreeCAD.ExpressionSecurity.principalOf
        live = principalOf(self.doc.Name)
        # the text is the source document's code, not this one's
        self.lib.Text = BRACKETS.replace("k = 3", "k = 9")
        self.assertEqual(principalOf(self.doc.Name), live)
        # a pinned snapshot is this document's code
        link.Pinned = True
        pinned = principalOf(self.doc.Name)
        self.assertNotEqual(pinned, live)
        # and retargeting the link is a change of this document's code
        link.Pinned = False
        other = self.source.addObject("App::ExpressionLibrary", "Other")
        other.Text = BRACKETS
        link.Source = other
        self.assertNotEqual(principalOf(self.doc.Name), live)
