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
