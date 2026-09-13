# SPDX-License-Identifier: LGPL-2.1-or-later
"""Expression libraries: a document's code, imported by its expressions.

docs/Sandbox.md 7.17 (c), the library half of the SandboxProgram gate (D2).
An App::ExpressionLibrary holds engine statements.  `import <Module>` in an
expression of the same document runs them once and binds what they left as a
module, whose functions resolve the module's names -- not their caller's.
Importing a library is a dependency on it.

The library and linked cases run natively; the routed twin of each is the
gtest ExpressionRoutingTest.libraries*, which needs the guest.  The fixture
cases (D3) open the committed programs of TestData/SandboxProgram routed and
compare them with the same files recomputed natively.
"""

import math
import os
import shutil
import tempfile
import unittest
import zipfile

import FreeCAD

import SandboxProgramFixtures as Fixtures
from FeaturePythonChain import CALLS, FeatureProxy, hooks, recordingDecline

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


# -- D3: the committed programs, routed = native ---------------------------------

SANDBOX = "User parameter:BaseApp/Preferences/Expression/Sandbox"
SECURITY = "User parameter:BaseApp/Preferences/Expression/Security"
DOCUMENT = "User parameter:BaseApp/Preferences/Document"

# the bracket library without its cleaning step: the fused shape keeps the faces
# removeSplitter() would have merged, so an edit is visible in the face count
UNCLEANED = Fixtures.BRACKET_LIBRARY.replace(".removeSplitter()", "")


class ParamGuard:
    """Boolean preferences set for a case and put back exactly as they were found.

    Every one of them is PERSISTED: a case that left Enforce off would disarm
    the permission tests of every later run on the box.
    """

    def __init__(self):
        self._saved = {}

    def _remember(self, group, key):
        if (group, key) not in self._saved:
            params = FreeCAD.ParamGet(group)
            had = key in params.GetBools()
            self._saved[(group, key)] = params.GetBool(key, False) if had else None

    def set(self, group, key, value):
        self._remember(group, key)
        FreeCAD.ParamGet(group).SetBool(key, value)

    def remove(self, group, key):
        self._remember(group, key)
        FreeCAD.ParamGet(group).RemBool(key)

    def restore(self):
        for (group, key), prior in self._saved.items():
            if prior is None:
                FreeCAD.ParamGet(group).RemBool(key)
            else:
                FreeCAD.ParamGet(group).SetBool(key, prior)
        self._saved = {}


def closeAll():
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)


def rewriteDocumentXml(path, old, new):
    """Edit a saved file's Document.xml in place, as a tamperer would."""
    with zipfile.ZipFile(path) as zin:
        comment = zin.comment
        entries = [(info, zin.read(info.filename)) for info in zin.infolist()]
    temp = path + ".tmp"
    with zipfile.ZipFile(temp, "w", zipfile.ZIP_DEFLATED) as zout:
        zout.comment = comment
        for info, data in entries:
            if info.filename == "Document.xml":
                text = data.decode("utf-8")
                if text.count(old) != 1:
                    raise ValueError("%r occurs %d times" % (old, text.count(old)))
                data = text.replace(old, new).encode("utf-8")
            zout.writestr(info, data)
    os.replace(temp, path)


class SandboxProgramFixtureTextCases(unittest.TestCase):
    """The committed fixtures still carry the texts SandboxProgramFixtures holds.

    A text changed in the module without regenerating the files would leave
    every routed case below testing the old program.  Runs without a guest.
    """

    def setUp(self):
        try:
            import Part  # noqa: F401
            import Spreadsheet  # noqa: F401
        except ImportError:
            self.skipTest("the Part and Spreadsheet modules are needed")
        self.directory = Fixtures.fixtureDirectory()

    def tearDown(self):
        closeAll()

    def open(self, name):
        return FreeCAD.openDocument(os.path.join(self.directory, name))

    def assertParameters(self, obj, parameters):
        for prop, value in parameters:
            got = getattr(obj, prop)
            self.assertEqual(getattr(got, "Value", got), value, "%s.%s" % (obj.Name, prop))

    def assertShapeExpression(self, obj, text):
        # compared as printed, so set the text again and see that nothing moves
        stored = dict(obj.ExpressionEngine)["Shape"]
        obj.setExpression("Shape", text)
        self.assertEqual(dict(obj.ExpressionEngine)["Shape"], stored, obj.Name)

    def testProgramsAndLibraries(self):
        F = Fixtures
        steps = (("Steps", F.STAIR_STEPS),)
        bolts = (("Bolts", F.FLANGE_BOLTS),)
        cases = (
            (F.BRACKET_FILE, "BracketExpr", F.BRACKET_EXPRESSION, F.BRACKET_PARAMETERS),
            (F.BRACKET_FILE, "BracketLib", F.BRACKET_CONSUMER, F.BRACKET_PARAMETERS),
            (F.STAIR_FILE, "StairExpr", F.STAIR_EXPRESSION, F.STAIR_PARAMETERS + steps),
            (F.STAIR_FILE, "StairLib", F.STAIR_CONSUMER, F.STAIR_PARAMETERS + steps),
            (F.FLANGE_FILE, "FlangeExpr", F.FLANGE_EXPRESSION, F.FLANGE_PARAMETERS + bolts),
            (F.CONSUMER_FILE, "Bracket", F.BRACKET_CONSUMER, F.BRACKET_PARAMETERS),
        )
        for name, objName, text, parameters in cases:
            doc = self.open(name)
            obj = doc.getObject(objName)
            self.assertIsNotNone(obj, "%s: %s" % (name, objName))
            self.assertParameters(obj, parameters)
            self.assertShapeExpression(obj, text)
            closeAll()

        for name, module, text in (
            (F.BRACKET_FILE, "brackets", F.BRACKET_LIBRARY),
            (F.STAIR_FILE, "stairs", F.STAIR_LIBRARY),
            (F.SOURCE_FILE, "brackets", F.BRACKET_LIBRARY),
        ):
            doc = self.open(name)
            self.assertEqual(FreeCAD.ExpressionSandbox.libraries(doc)[module].Text, text, name)
            closeAll()

    def testLinkedPair(self):
        doc = self.open(Fixtures.CONSUMER_FILE)
        link = doc.getObject("LibB")
        self.assertEqual(link.Module, "brackets")
        self.assertFalse(link.Pinned)
        self.assertIsNotNone(link.Source, "the source file did not load")
        self.assertEqual(
            os.path.normcase(link.Source.Document.FileName),
            os.path.normcase(os.path.join(self.directory, Fixtures.SOURCE_FILE)),
        )

    def testSheetType(self):
        F = Fixtures
        doc = self.open(F.SHEET_FILE)
        sheet = doc.getObject("Type")
        stored = sheet.getContents("A1")
        sheet.set("A1", F.FLANGE_METHOD)
        self.assertEqual(sheet.getContents("A1"), stored)
        for name, parameters, bolts in (
            ("Flange", F.FLANGE_PARAMETERS, F.FLANGE_BOLTS),
            ("Flange2", F.FLANGE2_PARAMETERS, F.FLANGE2_BOLTS),
        ):
            obj = doc.getObject(name)
            self.assertEqual(obj.ProxyExp, [sheet])
            self.assertParameters(obj, parameters + (("Bolts", bolts),))


class SandboxProgramFixtureCases(unittest.TestCase):
    """The committed programs: routed = native, byte for byte (7.17 (e), D3).

    TestData/SandboxProgram holds the bracket, the stair and the flange, each
    as an expression on a Part::Feature's Shape and as code on a linked object
    -- a library for the bracket and the stair, a sheet in ProxyExp for the
    flange -- plus the bracket library in one file and a consumer in another.
    Each file opens with routing ON and recomputes under enforcement, and every
    Shape is BRep byte-identical to the same file recomputed natively with
    enforcement off: natively `import Part` is host.import, a PROMPT for a
    document, so that is the only way the native twin runs.

    Every case works on a copy of the fixtures, since cases write.
    OptimizeRecompute is forced ON, its default: with it off the recompute
    cases would pass for the wrong reason.
    """

    PROGRAM_FILES = (
        Fixtures.BRACKET_FILE,
        Fixtures.STAIR_FILE,
        Fixtures.FLANGE_FILE,
        Fixtures.SHEET_FILE,
    )

    def setUp(self):
        if not FreeCAD.ExpressionSandbox.available():
            self.skipTest("no sandbox guest in this build")
        try:
            import Part  # noqa: F401
            import Spreadsheet  # noqa: F401
        except ImportError:
            self.skipTest("the Part and Spreadsheet modules are needed")
        self.directory = tempfile.mkdtemp()
        for name in Fixtures.FILES:
            shutil.copy(os.path.join(Fixtures.fixtureDirectory(), name), self.directory)
        self.params = ParamGuard()
        self.params.set(DOCUMENT, "OptimizeRecompute", True)
        del CALLS[:]

    def tearDown(self):
        closeAll()
        self.params.restore()
        shutil.rmtree(self.directory, ignore_errors=True)
        del CALLS[:]

    # -- helpers -----------------------------------------------------------------

    def path(self, name):
        return os.path.join(self.directory, name)

    def route(self, routed):
        """Routed under enforcement, or native with enforcement off."""
        if routed:
            self.params.set(SANDBOX, "Evaluate", True)
            self.params.set(SECURITY, "Enforce", True)
        else:
            self.params.remove(SANDBOX, "Evaluate")
            self.params.set(SECURITY, "Enforce", False)
        self.assertEqual(FreeCAD.ExpressionSandbox.routed(), routed)

    def open(self, name, routed):
        self.route(routed)
        return FreeCAD.openDocument(self.path(name))

    def loaded(self, name):
        """The document a link opened for the file `name`."""
        path = os.path.normcase(self.path(name))
        found = [
            d for d in FreeCAD.listDocuments().values() if os.path.normcase(d.FileName) == path
        ]
        self.assertEqual(len(found), 1, name)
        return found[0]

    def assertValid(self, doc):
        invalid = [o.Name for o in doc.Objects if "Invalid" in o.State]
        self.assertEqual(invalid, [], doc.FileName)

    def recomputeAll(self, doc):
        """Every object recomputed, not only what the file left touched."""
        for obj in doc.Objects:
            obj.touch()
        doc.recompute()
        self.assertValid(doc)

    def breps(self, doc):
        return {
            o.Name: o.Shape.exportBrepToString()
            for o in doc.Objects
            if o.isDerivedFrom("Part::Feature")
        }

    def build(self, name, routed, edit=None):
        """The file's BReps after a full recompute and then `edit(doc)` recomputed."""
        doc = self.open(name, routed)
        try:
            before = FreeCAD.ExpressionSandbox.evalCount()
            self.recomputeAll(doc)
            if routed:
                self.assertGreater(FreeCAD.ExpressionSandbox.evalCount(), before, name)
            if edit:
                edit(doc)
                doc.recompute()
                self.assertValid(doc)
            return self.breps(doc)
        finally:
            closeAll()

    def assertSameBreps(self, routed, native):
        self.assertEqual(sorted(routed), sorted(native))
        for name in routed:
            self.assertTrue(routed[name], name)
            self.assertTrue(routed[name] == native[name], "%s: the BRep differs" % name)

    def assertSameGeometry(self, routed, native, ulps=4):
        """Byte for byte, or else every vertex within `ulps` ULPs of native.

        The tolerance is the guest's libm: its sin and cos round differently
        from the host's for some arguments, one ULP at a time, so a bolt
        circle is byte-identical for one radius and a ULP off for the next.
        """
        import Part

        self.assertEqual(sorted(routed), sorted(native))
        for name in routed:
            if routed[name] == native[name]:
                continue
            r, n = Part.Shape(), Part.Shape()
            r.importBrepFromString(routed[name])
            n.importBrepFromString(native[name])
            self.assertEqual(len(r.Faces), len(n.Faces), name)
            rv = sorted((v.X, v.Y, v.Z) for v in r.Vertexes)
            nv = sorted((v.X, v.Y, v.Z) for v in n.Vertexes)
            self.assertEqual(len(rv), len(nv), name)
            for a, b in zip(rv, nv):
                for x, y in zip(a, b):
                    bound = ulps * math.ulp(max(abs(x), abs(y), 1.0))
                    self.assertLessEqual(abs(x - y), bound, "%s: %r against %r" % (name, a, b))

    # -- routed = native ---------------------------------------------------------

    def testProgramsRoutedMatchNative(self):
        for name in self.PROGRAM_FILES:
            with self.subTest(name):
                self.assertSameBreps(self.build(name, True), self.build(name, False))

    def testLinkedPairRoutedMatchesNative(self):
        routed = self.build(Fixtures.CONSUMER_FILE, True)
        self.assertSameBreps(routed, self.build(Fixtures.CONSUMER_FILE, False))
        # and it is the bracket: the source's text is the same library
        self.assertEqual(routed["Bracket"], self.build(Fixtures.BRACKET_FILE, True)["BracketLib"])

    def testTheTwoFormsAgree(self):
        # the expression and its method form build the same solid
        import Part

        bracket = self.build(Fixtures.BRACKET_FILE, True)
        self.assertEqual(bracket["BracketExpr"], bracket["BracketLib"])
        stair = self.build(Fixtures.STAIR_FILE, True)
        self.assertEqual(stair["StairExpr"], stair["StairLib"])
        flange = self.build(Fixtures.FLANGE_FILE, True)
        sheet = self.build(Fixtures.SHEET_FILE, True)

        def shape(brep):
            s = Part.Shape()
            s.importBrepFromString(brep)
            return s

        # not byte for byte: the method writes obj.Shape on a FeaturePython,
        # whose placement leaves a location record in the BRep that the
        # expression's Shape does not carry -- the geometry is the same to
        # the bit
        expr, method = shape(flange["FlangeExpr"]), shape(sheet["Flange"])
        self.assertEqual(
            sorted((v.X, v.Y, v.Z) for v in expr.Vertexes),
            sorted((v.X, v.Y, v.Z) for v in method.Vertexes),
        )
        self.assertEqual(expr.Volume, method.Volume)

        for brep, faces, volume in (
            (bracket["BracketExpr"], 8, 7500.0),
            (stair["StairExpr"], 21, 360000.0),
            (flange["FlangeExpr"], 10, 18749.024957),
            (sheet["Flange2"], 12, None),
        ):
            s = shape(brep)
            self.assertEqual(len(s.Faces), faces)
            if volume is not None:
                self.assertAlmostEqual(s.Volume, volume, places=5)

    def testParameterChangeMatchesNative(self):
        def setAll(prop, value):
            def edit(doc):
                for obj in doc.Objects:
                    if prop in obj.PropertiesList:
                        setattr(obj, prop, value)

            return edit

        def sheetEdit(doc):
            doc.getObject("Flange").Pcd = 40
            doc.getObject("Flange2").Bolts = 5

        for name, edit in (
            (Fixtures.BRACKET_FILE, setAll("Length", 55)),
            (Fixtures.STAIR_FILE, setAll("Steps", 6)),
            (Fixtures.FLANGE_FILE, setAll("Bolts", 8)),
            (Fixtures.SHEET_FILE, sheetEdit),
        ):
            with self.subTest(name):
                routed = self.build(name, True, edit)
                self.assertSameGeometry(routed, self.build(name, False, edit))
                unedited = self.build(name, True)
                self.assertTrue(
                    all(routed[k] != unedited[k] for k in routed), "the edit changed nothing"
                )

    def testSaveAndReopen(self):
        for name in self.PROGRAM_FILES + (Fixtures.CONSUMER_FILE,):
            with self.subTest(name):
                doc = self.open(name, True)
                self.recomputeAll(doc)
                before = self.breps(doc)
                saved = self.path("saved-" + name)
                doc.saveAs(saved)
                closeAll()
                doc = FreeCAD.openDocument(saved)
                self.recomputeAll(doc)
                self.assertEqual(self.breps(doc), before)
                closeAll()

    # -- the library -------------------------------------------------------------

    def testLibraryEditRecomputesItsConsumers(self):
        doc = self.open(Fixtures.BRACKET_FILE, True)
        self.recomputeAll(doc)
        expression = doc.getObject("BracketExpr").Shape.exportBrepToString()
        consumer = doc.getObject("BracketLib")
        self.assertEqual(len(consumer.Shape.Faces), 8)

        doc.getObject("Lib").Text = UNCLEANED
        doc.recompute()
        self.assertValid(doc)
        self.assertGreater(len(consumer.Shape.Faces), 8)
        # the expression form imports nothing, and is left alone
        self.assertEqual(doc.getObject("BracketExpr").Shape.exportBrepToString(), expression)

    def testTamperedLibraryIsAnotherPrincipal(self):
        """The library's text is the file's code: edited on disk, the grants are gone."""
        security = FreeCAD.ExpressionSecurity
        path = self.path(Fixtures.BRACKET_FILE)
        doc = FreeCAD.openDocument(path)
        principal = security.principalOf(doc.Name)
        closeAll()
        security.grant(principal, "host.import", "Part", True, "always")
        self.addCleanup(security.revoke, principal, "host.import", "Part")
        self.assertEqual(security.resolve(principal, "host.import", "Part"), "allow")

        # natively and enforced, the grant is what lets the programs import Part
        self.params.remove(SANDBOX, "Evaluate")
        self.params.set(SECURITY, "Enforce", True)
        doc = FreeCAD.openDocument(path)
        self.assertEqual(security.principalOf(doc.Name), principal)
        self.recomputeAll(doc)
        closeAll()

        rewriteDocumentXml(
            path, "return base.fuse(wall).removeSplitter()", "return base.fuse(wall)"
        )
        doc = FreeCAD.openDocument(path)
        tampered = security.principalOf(doc.Name)
        self.addCleanup(security.clearPending, tampered, "host.import", "Part")
        self.assertNotEqual(tampered, principal)
        self.assertNotIn(tampered, [g["principal"] for g in security.grants()])
        self.assertEqual(security.resolve(tampered, "host.import", "Part"), "prompt")
        for obj in doc.Objects:
            obj.touch()
        doc.recompute()
        self.assertIn("Invalid", doc.getObject("BracketExpr").State)

    # -- the linked pair ---------------------------------------------------------

    def testLinkedLiveFollowsAnEdit(self):
        doc = self.open(Fixtures.CONSUMER_FILE, True)
        self.recomputeAll(doc)
        bracket = doc.getObject("Bracket")
        self.assertEqual(len(bracket.Shape.Faces), 8)
        self.loaded(Fixtures.SOURCE_FILE).getObject("Lib").Text = UNCLEANED
        doc.recompute()
        self.assertValid(doc)
        self.assertGreater(len(bracket.Shape.Faces), 8)

    def testPinnedOpensWithoutTheSource(self):
        doc = self.open(Fixtures.CONSUMER_FILE, True)
        self.recomputeAll(doc)
        expected = doc.getObject("Bracket").Shape.exportBrepToString()
        doc.getObject("LibB").Pinned = True
        doc.save()
        closeAll()
        os.remove(self.path(Fixtures.SOURCE_FILE))

        doc = FreeCAD.openDocument(self.path(Fixtures.CONSUMER_FILE))
        self.assertEqual(list(FreeCAD.listDocuments()), [doc.Name])
        self.recomputeAll(doc)
        self.assertEqual(doc.getObject("Bracket").Shape.exportBrepToString(), expected)

    def testUnpinTakesTheSourcesText(self):
        doc = self.open(Fixtures.CONSUMER_FILE, True)
        link = doc.getObject("LibB")
        link.Pinned = True
        self.recomputeAll(doc)
        bracket = doc.getObject("Bracket")

        self.loaded(Fixtures.SOURCE_FILE).getObject("Lib").Text = UNCLEANED
        doc.recompute()
        self.assertEqual(len(bracket.Shape.Faces), 8, "a pinned library followed its source")

        link.Pinned = False
        self.assertEqual(link.Snapshot, "")
        doc.recompute()
        self.assertValid(doc)
        self.assertGreater(len(bracket.Shape.Faces), 8)

    # -- the sheet as the type ---------------------------------------------------

    def testMethodEditRebuildsBothInstancesAndAnUnrelatedCellNone(self):
        doc = self.open(Fixtures.SHEET_FILE, True)
        sheet = doc.getObject("Type")
        sheet.set("B1", "=1")
        recorder = doc.addObject("App::FeaturePython", "Recorder")
        recorder.expExecute = recordingDecline
        instances = [doc.getObject("Flange"), doc.getObject("Flange2")]
        for obj in instances:
            obj.ProxyExp = [recorder, sheet]
        doc.recompute()
        self.assertValid(doc)
        volumes = [obj.Shape.Volume for obj in instances]
        del CALLS[:]

        # an unrelated cell of the type moves: no instance recomputes
        sheet.set("B1", "=2")
        doc.recompute()
        self.assertEqual(hooks("Recorder", "expExecute"), [])

        # the method edited: both instances rebuilt, each with its own numbers
        sheet.set("A1", Fixtures.FLANGE_METHOD.replace("obj.HoleDia / 2", "obj.HoleDia"))
        doc.recompute()
        self.assertValid(doc)
        rebuilt = sorted(call[2] for call in hooks("Recorder", "expExecute"))
        self.assertEqual(rebuilt, ["Flange", "Flange2"])
        for obj, volume in zip(instances, volumes):
            self.assertLess(obj.Shape.Volume, volume, obj.Name)

    def testPlainConsumerOfTheSheetFollowsItsOwnCellOnly(self):
        # the property-level dependency Sheet::getRevision() == 0 protects, on
        # the same sheet that is the flange's type
        doc = self.open(Fixtures.SHEET_FILE, True)
        sheet = doc.getObject("Type")
        sheet.set("C1", "=2")
        sheet.setAlias("C1", "mine")
        sheet.set("D1", "=7")
        consumer = doc.addObject("App::FeaturePython", "Consumer")
        consumer.addProperty("App::PropertyInteger", "Marker")
        consumer.setExpression("Marker", "Type.mine")
        consumer.Proxy = FeatureProxy()
        doc.recompute()
        self.assertEqual(consumer.Marker, 2)
        del CALLS[:]

        sheet.set("D1", "=8")
        doc.recompute()
        self.assertEqual(hooks("P", "execute"), [])

        sheet.set("C1", "=3")
        doc.recompute()
        self.assertEqual(consumer.Marker, 3)
        self.assertEqual(len(hooks("P", "execute")), 1)
