# Document programs: shapes a file computes for itself

A tutorial.  The design, the rulings and the measurements are
docs/Sandbox.md 7.17 (the document program) and docs/ProxyChain.md (the
proxy chain); this page is how to write one.

A **document program** is code a `.FCStd` file carries, written in the
expression engine's language, that builds geometry through the `Part`
surface.  It needs no Python module, no add-on and no macro: the code is a
property value, it is saved with the file, and it runs in the sandbox guest
when routing is on.  There are three places to put it, and this page builds
the same three parts in each:

- **an expression bound to `Shape`** of a `Part::Feature` -- one object, one
  program, its parameters as the object's own properties;
- **a library**, an `App::ExpressionLibrary` object whose text is a module
  that any expression of the document imports -- functions written once,
  shared by many objects, and linkable from another file;
- **a sheet as a type**: a `Spreadsheet::Sheet` cell holding a method, and a
  `Part::FeaturePython` that links the sheet in its `ProxyExp` -- the sheet
  is the class, each linked feature an instance.

The three parts -- the bracket, the stair and the flange -- are the committed
fixtures in `src/Mod/Test/TestData/SandboxProgram`, written from the texts in
`src/Mod/Test/SandboxProgramFixtures.py`, and the texts below are those texts.
The `SandboxProgram` test module opens every one of them routed and checks
each shape against the same file recomputed natively: byte-identical as
committed, and within a few ULPs after a parameter change (section 8 says
why not always to the byte).


## 1. Before you start: the language in five lines

The language is Python-shaped statements: assignment, `if`/`elif`/`else`,
`while`, `for ... in`, `def`, `lambda`, `return`, `import` and
`from ... import`, lists, tuples, dicts, comprehensions.  It has **no
classes**.  Units are literals (`5mm`, `360deg`), and the engine builtins
(`vector`, `placement`, `rotation`, `cos`, `sin`, `sqrt`, `len`, `str`, ...)
are there without an import.

Four traps catch everyone once:

1. **A comma straight after a digit is a decimal comma.**  `min(1,2)` is
   `1.2`, and `vector(1,2,3)` does not parse.  Put a space after every comma.
2. **There is no `range`.**  Count with `while`.  (`for i in range(n)` does
   run routed, because the guest is Python, but not natively, and a program
   should mean the same thing both ways.)
3. **A line holding only a comment is a syntax error.**  A comment after code
   on the same line is fine.
4. **`pi` is a keyword**, so `math.pi` does not parse; write `pi`.

The last expression statement of a program is its value.  In a library, the
names the text leaves defined are the module's contents.


## 2. The bracket as an expression

Make a `Part::Feature` (Python: `doc.addObject("Part::Feature", "Bracket")`),
give it three `App::PropertyLength` properties -- `Length` 40 mm, `Width`
20 mm, `Thick` 5 mm -- and bind this to its `Shape` in the expression editor
(it takes several lines; Python: `obj.setExpression("Shape", text)`):

    import Part
    base = Part.makeBox(Length, Width, Thick)
    wall = Part.makeBox(Thick, Width, Length)
    base.fuse(wall).removeSplitter()

`Length`, `Width` and `Thick` are the object's own properties, so changing one
in the property editor recomputes the bracket.  The value of the last line --
the fused, cleaned solid -- lands in `Shape`.  In the file it is one
`<Expression path="Shape" .../>` entry in the object's `ExpressionEngine`.


## 3. The stair: a loop

`Part::Feature` again, with `Run` 25 mm, `Width` 80 mm, `Rise` 18 mm and an
`App::PropertyInteger` `Steps` of 4:

    import Part
    stair = Part.makeBox(Run, Width, Rise)
    i = 1
    while i < Steps:
        step = Part.makeBox(Run, Width, Rise * (i + 1))
        stair = stair.fuse(step.translated(vector(Run * i, 0, 0)))
        i = i + 1
    stair

Each step is a box one rise taller than the last, moved one run along X.
`translated` returns a moved copy and leaves `step` alone; `translate` would
move `step` itself.


## 4. The flange: a function

Six parameters -- `Dia` 60 mm, `Thick` 8 mm, `Bore` 20 mm, `Pcd` (the bolt
circle) 44 mm, `HoleDia` 6 mm, all lengths, and `Bolts`, an integer, 6:

    import Part
    def hole(a):
        return Part.makeCylinder(HoleDia / 2, Thick * 3, vector(Pcd / 2 * cos(a), Pcd / 2 * sin(a), -Thick))
    body = Part.makeCylinder(Dia / 2, Thick)
    body = body.cut(Part.makeCylinder(Bore / 2, Thick * 3, vector(0, 0, -Thick)))
    i = 0
    while i < Bolts:
        body = body.cut(hole(i * 360deg / Bolts))
        i = i + 1
    body

`hole` reads `HoleDia`, `Pcd` and `Thick` from inside its body.  That works,
but note what it means: **a function body's names are read when the function
is called, and they are not dependencies.**  Here it is harmless, because the
same names appear outside the body too, so the expression depends on them
anyway.  Section 7 is where it matters.


## 5. A library: write the function once

Put the code in an `App::ExpressionLibrary` (Python:
`doc.addObject("App::ExpressionLibrary", "Lib")`) and every expression of the
document can import it.  Set `Module` to `brackets` and `Text` to:

    import Part

    def bracket(L, W, T):
        base = Part.makeBox(L, W, T)
        wall = Part.makeBox(T, W, L)
        return base.fuse(wall).removeSplitter()

A `Part::Feature` with the same three parameters then binds only this to its
`Shape`:

    from brackets import bracket
    bracket(Length, Width, Thick)

and builds exactly the solid of section 2, to the byte.  The stair as a
library (`Module` `stairs`):

    import Part

    def step(run, width, rise, i):
        return Part.makeBox(run, width, rise * (i + 1)).translated(vector(run * i, 0, 0))

    def stair(steps, run, width, rise):
        shape = Part.makeBox(run, width, rise)
        i = 1
        while i < steps:
            shape = shape.fuse(step(run, width, rise, i))
            i = i + 1
        return shape

and its consumer:

    from stairs import stair
    stair(Steps, Run, Width, Rise)

What to know about libraries:

- **The text runs once**, top to bottom, the first time the document imports
  the module, and again only after the text changes.  Module-level values (a
  table of sizes, a constant) are computed once and shared.
- **Importing is a dependency.**  Edit `Text` and every object importing the
  module recomputes; that includes a module imported through another library.
- **A library's functions see the library, not the caller.**  `stair` calls
  `step`, defined above it, and it would equally find one defined below it; it
  never sees the consumer's variables.  Hand a library function its inputs as
  arguments.
- **The module name must not be spelled like an object.**  `Lib.bracket`
  parses as a property of the object `Lib`.  An empty `Module` answers to the
  object's name with its first letter lower-cased (`lib`).
- **A syntax error shows on the library**, which turns Invalid; its consumers
  wait.
- **The text is the file's code.**  It is part of the document's security
  principal: edit it outside FreeCAD and the permissions you granted the file
  no longer apply to it.


## 6. A library in another file

A second document imports a library it does not hold by linking it.  Add an
`App::ExpressionLibrary` there, set `Source` to the library in the other file
(the link picker, or `LibB.Source = other.getObject("Lib")`), and give it the
`Module` name this document wants to use -- it need not be the source's.
The fixture pair is `ProgramBracketsSource.FCStd` (the library of section 5)
and `ProgramBracketsConsumer.FCStd`, whose `LibB` links it and whose `Bracket`
imports `brackets` exactly as in section 5.  Opening the consumer opens the
source file with it.

Two ways to hold the link:

- **Live** (`Pinned` false): the consumer follows the source.  Edit the
  source's `Text`, recompute the consumer, and it rebuilds.  With the source
  file missing, the linked library reports "Source not found" and the
  consumers wait.
- **Pinned** (`Pinned` true): the source's text is copied into `Snapshot` at
  that moment, and the consumer uses the copy.  Edits in the source no longer
  arrive, and the consumer opens and recomputes with the source file absent
  -- which is what makes the file shareable.  Unpin, and it follows the source
  again (the snapshot is cleared).

Pinning is how you make a library you depend on stop changing under you.  A
live link is someone else's code running in your file with your file's
permissions; a pin is your copy of it.


## 7. The sheet as a type: methods on features

The third place turns the flange into a reusable type.  Make a
`Spreadsheet::Sheet` named `Type`, and put in cell `A1` this content (the
leading `=` is the cell's; a `def` in a cell names the cell after the
function, so `A1` becomes the alias `expExecute`):

    =def expExecute(obj):
        import Part
        body = Part.makeCylinder(obj.Dia / 2, obj.Thick)
        body = body.cut(Part.makeCylinder(obj.Bore / 2, obj.Thick * 3, vector(0, 0, -obj.Thick)))
        i = 0
        while i < obj.Bolts:
            a = i * 360deg / obj.Bolts
            body = body.cut(Part.makeCylinder(obj.HoleDia / 2, obj.Thick * 3, vector(obj.Pcd / 2 * cos(a), obj.Pcd / 2 * sin(a), -obj.Thick)))
            i = i + 1
        obj.Shape = body
        return True

Then make a `Part::FeaturePython` named `Flange`, give it the six parameters
of section 4, and set its `ProxyExp` to `[Type]`.  Recompute: the feature asks
the sheet for `expExecute`, calls it with itself as `obj`, and the method
writes `obj.Shape`.  Returning `True` says "handled"; `False` would pass the
call on down the chain.  The result is the solid of section 4 to the bit --
every vertex and the volume -- though not to the byte: a `FeaturePython`'s
placement leaves a location record in the saved shape that the expression's
does not carry.

A second `Part::FeaturePython` (`Flange2` in the fixture: `Dia` 80 mm,
`Thick` 10 mm, `Bore` 30 mm, `Pcd` 60 mm, `HoleDia` 8 mm, `Bolts` 8) linking
the same sheet is a second instance with its own numbers.  Edit the method in
`A1` and both rebuild on the next recompute; edit any other cell of the sheet
and neither does.

How this form differs from an expression:

- **The method writes; an expression returns.**  `obj.Shape = body` is a
  document write to the feature itself, which is allowed.  The engine logs
  that an assignment "may break dependency tracking", and it means it: see the
  next point.
- **Inputs come through `obj`.**  The method's body is not a dependency of
  anything, so a method that read a sibling cell (`=def expExecute(obj): ...
  Pitch ...`) would change behaviour when that cell changes with nothing
  recomputing.  Put every input on the instance.  A value that must live on the
  sheet is bound on the instance with an ordinary expression
  (`Flange.Pitch = Type.pitch`), and that is tracked.
- **The sheet must not read its instances.**  `ProxyExp` is a real link, so a
  cell reading `Flange` makes a cycle; the feature stays Touched for good.
  A type that wants its instance reads it through `obj`.
- **It reaches every hook**, not only the shape: `expOnChanged`,
  `expMustExecute`, and through `ViewProxyExp` the icon and the tree.  See
  docs/ProxyChain.md.
- **It links across files for free** (`ProxyExp` takes objects of other
  documents), but only live; there is no pinned snapshot of a sheet.  Called
  by the chain, a method from another file runs as the feature's file.


## 8. Running it: routed and native

Under enforcement, a document program is meant to run **routed**
(`Expression/Sandbox:Evaluate`, or `FreeCAD.ExpressionSandbox.setRouting(True)`):
the program runs in the sandbox guest, every `Part` call crosses into the host
as a geometry call, and the shapes come back.  Natively, `import Part` in a
document's expression is a host import and asks for permission, so a build
with no guest shows the permission prompt rather than running the program.
The shapes are the same either way -- that is what the fixtures gate -- with
one qualification: the guest's `sin` and `cos` are not the host's C library,
and for some angles they round the other way by one ULP (`sin(240deg)` is
-0.8660254037844384 natively, ...385 routed).  A bolt circle can therefore be
byte-identical at one radius and one ULP off at the next; do not write a
program whose topology hangs on the last bit of a trigonometric result.  The
time goes into the OCCT booleans, not the language: the flange evaluates in
19.6 ms natively and 22.0 ms routed (15 bridge crossings), about what the same
solid costs from a Python `Proxy`.

What a program can reach is the curated `Part` surface (docs/Sandbox.md 7.17
(b)): the `make*` constructors, `extrude`, `revolve`, the booleans,
`makeFillet`, `makeChamfer`, `mirror`, `makeThickness`, `makeOffset2D`,
`translated`/`rotated`/`scaled`, the shape queries (`Volume`, `Area`,
`Faces`, `BoundBox`, `isValid`, ...).  No file I/O, no document creation, no
GUI.  The surface is versioned: a file records the version it was saved
against, and opening it on a build with a newer surface logs a note.


## 9. Where to look next

- `src/Mod/Test/SandboxProgramFixtures.py` -- the texts, and
  `writeFixtures()` to regenerate the six files.
- `src/Mod/Test/SandboxProgram.py` -- the library, the linked-library and the
  fixture cases.
- docs/Sandbox.md 7.17 -- why it is built this way; sec 12 for the traps.
- docs/ProxyChain.md -- the chain the sheet form rides on.
