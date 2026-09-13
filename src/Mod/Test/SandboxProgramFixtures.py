# SPDX-License-Identifier: LGPL-2.1-or-later
"""The document programs of docs/Sandbox.md 7.17 (a), and the files written from them.

Three programs in the expression engine's language -- the bracket, the stair,
the flange -- each written twice: as the expression bound to a Part::Feature's
Shape, and as code on a linked object (a library module for the bracket and
the stair, a sheet in ProxyExp for the flange).  The texts below are the one
source: the fixtures in TestData/SandboxProgram are written from them by
writeFixtures(), docs/DocumentPrograms.md quotes them, and SandboxProgram
checks that the committed files still carry them.

Regenerate the fixtures after changing a text (natively, with enforcement off
for the run, since natively `import Part` is a prompt for a document):

    ./.conda/run.sh build/conda-relwithdebinfo-801/bin/FreeCADCmd -c \\
      "import SandboxProgramFixtures as F; F.writeFixtures('src/Mod/Test/TestData/SandboxProgram')"

Language traps these texts respect (docs/Sandbox.md sec 12): a comma straight
after a digit is a decimal comma, so every argument list has a space after its
commas; the engine has no `range`, so the loops count with `while`; a line
holding only a comment is a syntax error.
"""

import os

import FreeCAD

SECURITY = "User parameter:BaseApp/Preferences/Expression/Security"

# -- the bracket: two boxes fused and cleaned, three parameters ---------------

BRACKET_EXPRESSION = (
    "import Part\n"
    "base = Part.makeBox(Length, Width, Thick)\n"
    "wall = Part.makeBox(Thick, Width, Length)\n"
    "base.fuse(wall).removeSplitter()\n"
)

BRACKET_LIBRARY = (
    "import Part\n"
    "\n"
    "def bracket(L, W, T):\n"
    "    base = Part.makeBox(L, W, T)\n"
    "    wall = Part.makeBox(T, W, L)\n"
    "    return base.fuse(wall).removeSplitter()\n"
)

BRACKET_CONSUMER = "from brackets import bracket\nbracket(Length, Width, Thick)\n"

BRACKET_PARAMETERS = (("Length", 40), ("Width", 20), ("Thick", 5))

# -- the stair: boxes translated and fused in a loop ---------------------------

STAIR_EXPRESSION = (
    "import Part\n"
    "stair = Part.makeBox(Run, Width, Rise)\n"
    "i = 1\n"
    "while i < Steps:\n"
    "    step = Part.makeBox(Run, Width, Rise * (i + 1))\n"
    "    stair = stair.fuse(step.translated(vector(Run * i, 0, 0)))\n"
    "    i = i + 1\n"
    "stair\n"
)

STAIR_LIBRARY = (
    "import Part\n"
    "\n"
    "def step(run, width, rise, i):\n"
    "    return Part.makeBox(run, width, rise * (i + 1)).translated(vector(run * i, 0, 0))\n"
    "\n"
    "def stair(steps, run, width, rise):\n"
    "    shape = Part.makeBox(run, width, rise)\n"
    "    i = 1\n"
    "    while i < steps:\n"
    "        shape = shape.fuse(step(run, width, rise, i))\n"
    "        i = i + 1\n"
    "    return shape\n"
)

STAIR_CONSUMER = "from stairs import stair\nstair(Steps, Run, Width, Rise)\n"

STAIR_PARAMETERS = (("Run", 25), ("Width", 80), ("Rise", 18))
STAIR_STEPS = 4

# -- the flange: a disc cut by a bore and a bolt circle, six parameters -------

FLANGE_EXPRESSION = (
    "import Part\n"
    "def hole(a):\n"
    "    return Part.makeCylinder(HoleDia / 2, Thick * 3,"
    " vector(Pcd / 2 * cos(a), Pcd / 2 * sin(a), -Thick))\n"
    "body = Part.makeCylinder(Dia / 2, Thick)\n"
    "body = body.cut(Part.makeCylinder(Bore / 2, Thick * 3, vector(0, 0, -Thick)))\n"
    "i = 0\n"
    "while i < Bolts:\n"
    "    body = body.cut(hole(i * 360deg / Bolts))\n"
    "    i = i + 1\n"
    "body\n"
)

# the content of the sheet cell: a def in a cell aliases the cell to its name
FLANGE_METHOD = (
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

FLANGE_PARAMETERS = (("Dia", 60), ("Thick", 8), ("Bore", 20), ("Pcd", 44), ("HoleDia", 6))
FLANGE_BOLTS = 6

# the second instance on the same sheet
FLANGE2_PARAMETERS = (("Dia", 80), ("Thick", 10), ("Bore", 30), ("Pcd", 60), ("HoleDia", 8))
FLANGE2_BOLTS = 8

# -- the files -----------------------------------------------------------------

BRACKET_FILE = "ProgramBracket.FCStd"
STAIR_FILE = "ProgramStair.FCStd"
FLANGE_FILE = "ProgramFlange.FCStd"
SOURCE_FILE = "ProgramBracketsSource.FCStd"
CONSUMER_FILE = "ProgramBracketsConsumer.FCStd"
SHEET_FILE = "ProgramFlangeSheet.FCStd"

FILES = (BRACKET_FILE, STAIR_FILE, FLANGE_FILE, SOURCE_FILE, CONSUMER_FILE, SHEET_FILE)


def fixtureDirectory():
    """Where the build and the install put the committed fixtures."""
    return os.path.join(FreeCAD.getHomePath(), "Mod", "Test", "TestData", "SandboxProgram")


def addParameters(obj, lengths, integers=()):
    for name, value in lengths:
        obj.addProperty("App::PropertyLength", name, "Program")
        setattr(obj, name, value)
    for name, value in integers:
        obj.addProperty("App::PropertyInteger", name, "Program")
        setattr(obj, name, value)
    return obj


def addLibrary(doc, module, text, name="Lib"):
    lib = doc.addObject("App::ExpressionLibrary", name)
    lib.Module = module
    lib.Text = text
    return lib


def addProgram(doc, name, text, lengths, integers=()):
    obj = addParameters(doc.addObject("Part::Feature", name), lengths, integers)
    obj.setExpression("Shape", text)
    return obj


def buildBracket(doc):
    addLibrary(doc, "brackets", BRACKET_LIBRARY)
    addProgram(doc, "BracketExpr", BRACKET_EXPRESSION, BRACKET_PARAMETERS)
    addProgram(doc, "BracketLib", BRACKET_CONSUMER, BRACKET_PARAMETERS)


def buildStair(doc):
    steps = (("Steps", STAIR_STEPS),)
    addLibrary(doc, "stairs", STAIR_LIBRARY)
    addProgram(doc, "StairExpr", STAIR_EXPRESSION, STAIR_PARAMETERS, steps)
    addProgram(doc, "StairLib", STAIR_CONSUMER, STAIR_PARAMETERS, steps)


def buildFlange(doc):
    addProgram(doc, "FlangeExpr", FLANGE_EXPRESSION, FLANGE_PARAMETERS, (("Bolts", FLANGE_BOLTS),))


def addFlangeInstance(doc, name, sheet, lengths, bolts):
    obj = addParameters(doc.addObject("Part::FeaturePython", name), lengths, (("Bolts", bolts),))
    obj.ProxyExp = [sheet]
    return obj


def buildFlangeSheet(doc):
    sheet = doc.addObject("Spreadsheet::Sheet", "Type")
    sheet.set("A1", FLANGE_METHOD)
    addFlangeInstance(doc, "Flange", sheet, FLANGE_PARAMETERS, FLANGE_BOLTS)
    addFlangeInstance(doc, "Flange2", sheet, FLANGE2_PARAMETERS, FLANGE2_BOLTS)


def writeFixtures(directory):
    """Write the six files into `directory`; returns their paths.

    The source library is saved before the consumer links it, so the link is
    stored relative to a file next to it and the pair moves as a pair.
    """
    import Spreadsheet  # noqa: F401  the sheet fixture needs the type

    directory = os.path.abspath(directory)
    os.makedirs(directory, exist_ok=True)
    params = FreeCAD.ParamGet(SECURITY)
    hadEnforce = "Enforce" in params.GetBools()
    oldEnforce = params.GetBool("Enforce", True)
    params.SetBool("Enforce", False)
    paths = []
    try:

        def save(doc, name):
            path = os.path.join(directory, name)
            doc.recompute()
            doc.saveAs(path)
            paths.append(path)
            return path

        for name, builder in (
            (BRACKET_FILE, buildBracket),
            (STAIR_FILE, buildStair),
            (FLANGE_FILE, buildFlange),
            (SHEET_FILE, buildFlangeSheet),
        ):
            doc = FreeCAD.newDocument("Program")
            try:
                builder(doc)
                save(doc, name)
            finally:
                FreeCAD.closeDocument(doc.Name)

        source = FreeCAD.newDocument("ProgramBracketsSource")
        consumer = FreeCAD.newDocument("ProgramBracketsConsumer")
        try:
            lib = addLibrary(source, "brackets", BRACKET_LIBRARY)
            save(source, SOURCE_FILE)
            consumer.saveAs(os.path.join(directory, CONSUMER_FILE))
            linked = consumer.addObject("App::ExpressionLibrary", "LibB")
            linked.Module = "brackets"
            linked.Source = lib
            addProgram(consumer, "Bracket", BRACKET_CONSUMER, BRACKET_PARAMETERS)
            save(consumer, CONSUMER_FILE)
        finally:
            FreeCAD.closeDocument(consumer.Name)
            FreeCAD.closeDocument(source.Name)
    finally:
        if hadEnforce:
            params.SetBool("Enforce", oldEnforce)
        else:
            params.RemBool("Enforce")
    # the consumer is saved twice, and a second save leaves a backup beside it
    for entry in os.listdir(directory):
        stem, dot, suffix = entry.rpartition(".FCStd")
        if dot and suffix.isdigit():
            os.remove(os.path.join(directory, entry))
    return paths
