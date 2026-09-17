# The Python sandbox: consolidated reference

Consolidated on **2026-09-03** from six documents written between
2026-08-29 and 2026-09-03 -- `ExpressionSandbox.md`,
`ExpressionSandboxPhase0.md`, `ExpressionImage.md`, `PyodideHost.md`,
`SandboxNetwork.md`, `SandboxGui.md` -- after an audit of every concrete
claim in them against the code at `dfd9336c91` on branch `SecurePython`.
Those six stay in the tree as the historical record (each carries a
banner pointing here); this document is the one to read and the one to
keep current.  Sec 14 says which section of which source went where and
what the audit found stale.

Status tags used throughout: **[built]** exists in the tree and is
tested; **[measured]** a number taken on this box under the stated
conditions; **[designed]** written down, not built; **[decided]** a
user decision, quoted where the wording matters.

## 0. At a glance

**Re-aimed 2026-09-08 (1.2): the sandbox boxes CODE CARRIED IN THE
DOCUMENT -- expressions and expression-language programs generating
parametric shapes through a curated geometry surface.  Installed
workbench code is not a target; the GUI stays native; the widget
layer built under sec 7 is the browser's toolkit.  The guest-only GUI
pieces are frozen, not extended.**

    area                            status      where
    ------------------------------  ----------  ------------------------------------------
    principals, permissions, grants  built       src/App/ExpressionSecurity*.{h,cpp}
    enforcement at the chokepoints   built       Expression.cpp, ObjectIdentifier.cpp
    permissions panel + padlock      built       src/Gui/DlgDocumentPermissions.{h,cpp}
    the image (engine in wasm)       built       src/App/ExpressionImage/
    the wire (ops, tags, handles)    built       src/App/ExpressionImage/FcxWire.h
    generated facades                built       src/Tools/bindings/generateSandboxFacades.py
    the router (expressions, sheet)  built       src/App/ExpressionEvaluator.{h,cpp}, gated OFF
    pyodide runtime (default)        built       src/App/ExpressionPyodideRuntime.cpp, PyodideHost/
    WASI runtime (reference)         built       src/App/ExpressionWasmtimeRuntime.cpp, off by default
    time budget (both runtimes)      built       ExpressionPyodideRuntime.cpp, ExpressionWasmtimeRuntime.cpp
    bootstrap + package set          built       src/App/ExpressionPyodide.cpp, src/Ext/freecad/pyodide/
    missing-package offer            built       pkg.missing op, Permission::PkgInstall, the panel
    corpus gate                      built       scripts/expr-switchover/
    porting linter (GUI)             built       scripts/sandbox_gui_lint.py
    GUI registration from the guest  built       src/Gui/SandboxGui.cpp, the guest FreeCADGui (7.9)
    Coin + pivy inside the guest     built       src/App/PyodideHost/pivy/, Coin's COIN_BUILD_GL_STUB (7.10)
    forms: ipywidgets over comm, Qt  built       src/App/ExpressionImage/widgets/ (guest), src/Ext/freecad/widgets/ (host), SandboxGui.cpp (7.3)
    forms: the Qt subset, .ui, panel built       G3a: freecad.widgets in the guest (the Qt classes as models), loadUi both sides, Control (7.11)
    forms: dialogs, item views, ...  built       G3b: exec_(), the tree/list/table family as rows, containers, the file chooser; the 40-file harness (7.11)
    host widget layer: core, Qt view built       H0: src/Gui/Fw/ (Fw:: models, FwQt:: backend, the store, FreeCADGui.FormWidgets), src/Tools/fwuic.py (7.12)
    native panels on the layer       sized       H1-H3: the first ports, the form-only majority, the item views; DOM walker later (7.4, 7.12)
    the task panel mirror            M3 built    7.19: the desktop's task panel walked into models, streamed (Pad, Draft's OrthoArray, a CAM op, no workbench edited); M2: item rows reflected (Sketcher's constraint list), pictures and icons by image id; M3: top-level dialogs as dialog:<n> roots (a panel slot's QMessageBox, its exec code from a client's click), mouse replay into pictures; M4 measured 2026-09-11 (sec 8.4: a repaint burst re-reads 10-20 widgets in 0.3 ms and sends nothing; a panel at rest sends nothing; a keystroke costs the other clients 70-150 B)
    the panels in the browser (G7)  W1 building 7.22: the DOM view over the widget layer -- the walker, the layout plan, the panel container, the item views; W1-W5, 2.2-3.3k of TypeScript in src/Gui/Renderer/web, and one 20-line host change (a client is never told how the host corrected its own write); the five questions RULED 2026-09-16 (chrome-flavoured, the echo taken, dialogs in scope, a FLOATING card, the pure-plan gate) and W1 started; W1's host half BUILT 2026-09-16 (Store::messageTo, the gate in test_widgetStream, FormWidgets 22/22), the fixture corpus (6 cases) recorded and the walker core, the layout plan and the replay gate BUILT 2026-09-16 (62 checks ALL GREEN) and the client + views + floating card BUILT the same day (typecheck and bundle clean; NOT yet rendered against a live desktop), W2 next
    the session document (commands) built       S1: a workbench reaches every open document, live ActiveDocument, app.write, save, picker-blessed saveAs; S2: Gui.doCommand / addModule in the guest under gui.doCommand, Draft's commit and Arch_Site end to end; gate SandboxSessionDoc (7.13)
    routing ON by default            built       preference Expression/Sandbox:Evaluate, ON since 2026-09-16: the corpus gate green (94 files, 195 of 195 same) and the restore half guarded by available() first
    Proxy import restriction (native) built       item 1 of sec 11: PropertyPythonObject restore
                                                 confined to the Mod roots, both containers
    the document program             D1 built    7.17: a Part::Feature's Shape expression is the
                                                 program; RE-SIZED 2026-09-13 against the proxy
                                                 chain -- a Sheet in ProxyExp is a second carrier,
                                                 the flange natively at the speed of a host-Python
                                                 Proxy (19.6 vs 21.3 ms); D1 BUILT 2026-09-13:
                                                 function objects in the image, the flange as one
                                                 expression routed = native to the BRep byte, the
                                                 surface annotated and versioned; a function value
                                                 still cannot LEAVE a routed evaluation (P3's
                                                 problem); per-document guests OUT (335 MB, 2.5 s);
                                                 D2's library half BUILT 2026-09-13:
                                                 App::ExpressionLibrary imported by the document's
                                                 own expressions, routed = native, the guest keeping
                                                 one module per principal; the linked library
                                                 (Source/Pinned/Snapshot) BUILT the same day, a
                                                 live link sharing the source's module; P3 BUILT
                                                 the same day: a routed function crosses as a
                                                 stand-in, the chain's call runs as the feature's
                                                 file (RULED); D3 BUILT 2026-09-14: the five
                                                 fixtures (six files) open routed = native, the
                                                 flange bench 1.12x native, the tutorial in
                                                 docs/DocumentPrograms.md; D4 BUILT 2026-09-14:
                                                 a memory ceiling per guest (MemoryMB on the
                                                 Python heap, EngineHeapMB on V8's), a refusal
                                                 as MemoryError, and the reset retention
                                                 measured: glibc's arenas, not a leak
    the abandoned rungs' code        frozen      1.6: RULED 2026-09-09 "freeze everything"; the
                                                 cut line kept as the record; 1.7 evaluates what
                                                 the workbench path would still take
    the proxy chain                  building    7.21 -> docs/ProxyChain.md: ProxyExp and
                                                 ViewProxyExp, XLinkLists of objects asked
                                                 for exp<Hook> / expView<Hook> before the
                                                 Proxy; the hook bodies one template, cog
                                                 for the table at build time; P0 and P1
                                                 BUILT 2026-09-12 (the refactor, then the
                                                 App-side chain), P2 BUILT 2026-09-13 (the
                                                 view side); the edited-method recompute
                                                 BUILT 2026-09-13 (ProxyChain.md 4.5, 4.6),
                                                 fine-grained per-cell, 8 gate cases; P3, the
                                                 sandbox, BUILT 2026-09-13 inside 7.17's D2
    the browser console              building    7.20: pyodide in the client's page, the wire
                                                 over the socket (JSPI), a client:<identity>
                                                 principal, catalog v2; C1-C6, one to two weeks;
                                                 RULED 2026-09-11 after the document program;
                                                 C1 BUILT 2026-09-14: the guest boots in a page
                                                 from the serving FreeCAD (/pyodide/, boot.json
                                                 behind the door), and JSPI is proven -- the
                                                 host import suspends mid-statement; C2 BUILT
                                                 2026-09-15: the page's statements read and
                                                 write the served document over the socket
                                                 (per-connection endpoint); C3 BUILT
                                                 2026-09-15: a client:<identity> principal,
                                                 catalog v2's column, view-only = read-only
                                                 bridge, gui a hard DENY for clients; C4
                                                 BUILT 2026-09-15: the console panel in the
                                                 viewer chrome, booted on first open; C5
                                                 BUILT 2026-09-15: a statement costs its ops x
                                                 RTT, and a prefetch of sibling reads makes a
                                                 loop a few ops (50 objects at 100 ms: 5.4 s ->
                                                 0.42 s); the guest is +185 MB PSS in the page
    host file / code chokepoints     designed    7.14: fs.read / fs.write / host.exec at the core's file and runFile primitives, keyed on the scope stack; closes Gui.runCommand("Std_RecentMacros") from a guest
    network capability               designed    sec 6
    GUI protocol, mirror, widgets    designed    sec 7 (U1, U3's wire and Qt manager, the guest's Coin are built)
    rungs 1-4 of the ladder          designed    sec 1.3
    memory ceiling for a guest       built       7.17 D4 (2026-09-14); what it does not bound, sec 13

## 1. Direction

### 1.1 Why

The expression engine is a hand-written C++ interpreter with no pure
C++ evaluation path: every value is a CPython object (`getValueAsAny()`
is `pyObjectToAny(getPyValue())`, about 128 call sites in
`Expression.cpp`).  Its old protection was a name-matched allowlist run
inside the interpreter it guarded -- a confused deputy: an eight-name
builtin denylist plus a module allowlist by `__module__` prefix, with
the module check on attribute-traversal results commented out.  The
threat the roadmap names is concrete: opening a malicious `.FCStd` runs
code at recompute.

The direction, ordered by the user (2026-08-29): sandbox the engine's
Python before the spreadsheet ships to the browser; define the security
model (who is asking, what can be asked, how the user grants) BEFORE
the mechanism, with grant as first-class as deny (2026-08-30).

### 1.2 The end state **[decided 2026-09-03; RE-AIMED 2026-09-08]**

The 2026-09-03 goal was "run everything Python in pyodide": one
preference, `Python/Runtime = native | pyodide`, moving every rung
that is ready into the sandbox.  Pyodide is THE runtime FreeCAD ships
and develops; the WASI image is the reference implementation, built
only on request.  That much stands.

**Re-aimed 2026-09-08, user ruling, after G4 was sized (7.16) and the
question "is it worth it, or restrict to expressions and code in the
document?" was put.**  The reasoning, kept because it decides the
list:

- What runs when a document OPENS in the GUI is the VIEW PROVIDER's
  code: `loads`, `attach`, `startRestoring`/`finishRestoring`, then
  `onChanged` and `updateData` per property, `getIcon` and
  `claimChildren` from the tree.  The App side runs `loads` and
  `onDocumentRestored`; `execute` runs on a recompute, not on open.
  So the App-side sandbox (G1) isolated the half that mostly does not
  run at open, and the half that does is native -- and boxing it means
  the mirror (G4), the residue that never ends: every sizing found the
  next ten APIs, and each is a surface tracking upstream Draft and BIM
  forever.
- In this fork NONE of a Proxy is document code: the state is JSON
  (no pickle; the regex in the restore path only scans legacy pickle
  text for a module and class name), and the class is chosen by
  module name.  A file supplies CODE only by naming a module outside
  the installed tree, or through an expression.  The native fix is the
  rule `Type::importModule` already has, applied to
  `PropertyPythonObject`'s restore for BOTH containers (sec 11 item 1).
  After it a document can only pick installed classes and feed them
  data; Draft and BIM have zero `eval`/`exec` on document data.
- The reason the GUI work went this far was two questions, both
  answered without the GUI in the box.  "How do the workbench half and
  the document half coexist, which side recomputes?": the HOST,
  always -- the document, its graph and every TopoShape are C++ on the
  host; a Proxy hook is one hop into the guest.  "I want the task panel
  in the browser": the browser tier is a THIN CLIENT (RoadMap.md sec
  3: no OCCT, no Python in the browser, DOM for the UI chrome), so a
  panel's logic runs on the host and the browser needs only a DOM view
  of the panel model -- which is what H0 and G3 built.  The widget
  layer is the browser's toolkit; the sandbox is not what puts a panel
  in the browser.
- **The target, stated by the user**: the expression engine as a
  language a DOCUMENT carries -- a user-defined, document-carried
  addon that generates parametric shapes -- which the native engine
  could already do before this branch (lambdas, assignments,
  callables, `Part` reachable) and which was exactly the hole of 1.1.
  A document-carried program produces SHAPES; shapes are App data
  shown by the standard native view providers; document code never
  needs a GUI half.  That is the one thing the sandbox boxes.

The end state, re-aimed: **a document's programs -- expressions and
expression-language libraries -- evaluate in the document's guest over
a curated, versioned geometry surface, with routing ON by default;
installed workbench code runs native, attributed, not enforced; the
GUI is native, and the widget layer serves the browser.**  The ladder's
rungs 3 and 4 (session scripting, Python workbenches in the guest) are
RETIRED as goals; what was built under them is frozen in place (sec 7
header, sec 11).

### 1.3 The ladder

Each rung ships alone; each is strictly more Python in the sandbox.

- **Rung 0, expressions** **[built]**: both spreadsheet modes route,
  the corpus gate passes, the budget works.  Routing is preference-gated
  and ON by default since 2026-09-16 (sec 3.5); OFF by default before.
- **Rung 1, native dispatch for `call` members** **[designed; DROPPED
  2026-09-08]**: the annotated method set retargeted from Python C API
  calls to generated C++ dispatch.  Dropped because it has no
  customer: the corpus (8.3) shows expressions make 302 member reads
  and two method calls in 7628 expressions, and a shape program's
  calls are OCCT-bound at hundreds of microseconds against a 5 us
  hop.  What expressions pay for is the fixed cost per evaluation
  (about 22 us of wire plus 23 us of bindings pack, 8.1) and property
  reads that materialize a Python object on the host (`Shape.BoundBox`
  builds a `TopoShapePy` first): the two performance items of sec 11
  are the pack cache and typed reads, not dispatch.
- **Rung 2, document-embedded Python** **[built as G1 for Draft and
  BIM; hardened mode, not the target, 2026-09-08]**: scripted-object
  `Proxy` code in the document's guest, `execute()` a bridge call --
  70/70 Draft Proxies and the BIM corpus recompute byte-identical
  (7.6).  With the import restriction (sec 11 item 1) it buys defense
  in depth and crash isolation for INSTALLED code against hostile
  data; kept and maintained, since expressions ride the same bridge.
  Per-document guests return to the list on a different ground: one
  file's runaway program must not stall another's recompute.
- **Rung 3, session scripting** **[RETIRED 2026-09-08]**: console and
  macros in a session guest.  What was built for it (S1, S2, G3d, the
  `gui.*` op family) is frozen.
- **Rung 4, Python workbenches** **[RETIRED 2026-09-08]**: the GUI
  residue of sec 7.  G2, G3 and the InitGui runner are frozen in place
  with the switch off; G4 (7.16) was sized and not built.

The App-core severance (`FREECAD_NO_NATIVE_PYTHON`, an App that does not
link libpython) is **DROPPED 2026-09-08**: Python is woven through the
core, not layered on it -- 105 of 197 files in `src/App` and 50 of 153
in `src/Base` touch the C API, 33 generated binding XMLs, every
expression value a CPython object -- so the split is months of
re-layering once and a permanent divergence in the most-merged files
(`Document.cpp`, the `Property` family, `Expression`) paid at every
upstream sync.  Its audit value is had by a grep over the generated
facades; its engineering value, a Python-free OCCT worker, is not
needed by ComputeBoundaries.md, whose boundary is around OCCT, not
around App.  `ExpressionCore` stays an OBJECT library folded into
`FreeCADApp` (`src/App/CMakeLists.txt:580-601`).

### 1.4 Standing rulings

- Expressions are DOCUMENT CODE: gate by principal only, never carve
  out a carrier (2026-09-03).  An expression, a Proxy, an embedded
  script all run as `document:<hash>`.
- Pre-resolve host-side, then FAIL CLOSED: no silent native fallback
  when the image cannot evaluate (2026-08-31).
- Routing stays OFF by default until the corpus regression, not
  judgement, says otherwise (2026-08-31).  It said otherwise on
  2026-09-16: 94 files, 195 expressions, 195 same, 0 differ.  The same
  preference also routes a saved Proxy's restore, which that gate does
  not measure, so the restore half was guarded by `available()` first
  (3.5) and the default flipped only after.
- wasmtime stays a SHARED library and a conda package
  (`wasmtime-capi`), decided on the multi-process OCCT direction and on
  more Python moving into the image, not on size (2026-09-01).
- `doCommand` is permission-controlled: runs under the caller's
  principal, never escalates, gated by `gui.doCommand` (2026-09-03).
- The GUI island (native PySide/pivy code) is attributed, not enforced:
  the threat model is document-derived code, which never runs there.
- The target is CODE CARRIED IN THE DOCUMENT (2026-09-08, 1.2):
  expressions and expression-language programs.  Installed workbench
  code, App or GUI side, is not a sandboxing target; the widget layer
  is the browser's toolkit, not a sandbox artifact.  No prompt ever
  hands a document the user's authority (the VBA lesson, 1.5): a
  document-side permission is ALLOW by the catalog or DENY
  non-promptable.

### 1.5 Prior art for code carried in a file **[recorded 2026-09-08]**

What a document-carried language looks like elsewhere, and what each
case teaches this design:

- **Onshape FeatureScript** -- the closest thing.  A purpose-built
  language for parametric features, stored in the document itself,
  run on the server, over a curated standard library on the Parasolid
  kernel: no I/O, value semantics, deterministic, and the standard
  library is VERSION-PINNED so a feature written a year ago evaluates
  the same.  Ours is the same shape over OCCT: a program in the file,
  the `geom.call` surface, a budget instead of a trust prompt.  Taken
  from it: version the surface -- a file records which facade version
  it was written against, so growing or tightening the annotated set
  never silently changes an old file.
- **Spreadsheets, where this engine came from.**  Excel's `LET` and
  `LAMBDA` made the formula language a functional language, and a
  `LAMBDA` bound to a defined name is a document-carried function
  library callable from any cell -- the carrier of sec 11 item 2,
  shipped and familiar.  Office Scripts and Google Apps Script are the
  other layer: real scripts, sandboxed, a curated API, scoped
  permissions.  What Microsoft did NOT do is make VBA safe; they built
  a second, restricted language beside it.
- **Whole-file programs.**  OpenSCAD: the model IS the program, the
  language small and functional, nothing to escape into because it has
  no I/O beyond including files.  Houdini's VEX inside the node
  network: compiled, side-effect-free, a curated library, kept apart
  from the unsandboxed Python layer by design.
- **Sandboxed scripting in hostile content.**  World of Warcraft addons
  and Roblox's Luau run user Lua under an allowlisted API with no file
  system or network; WoW's taint system -- code is secure or insecure
  by origin, protected calls refuse insecure callers -- is the analog
  of our principal attribution.  PDF JavaScript is the warning on this
  row: the sandbox held, the curated API surface is where the exploits
  lived.  Our surface is the annotated set plus the value codecs; that
  is what deserves the fuzzing.
- **The failure mode: code with the user's authority plus a prompt.**
  VBA, Excel 4.0 macros, Blender's auto-run scripts in `.blend` files,
  Emacs file-local `eval`.  Each ended the same way: the prompt trained
  users to click yes, then the vendor blocked the feature by default
  (Microsoft in 2022 via Mark of the Web, Blender by turning auto-run
  off).  Dynamo and Grasshopper carry unsandboxed Python and C# nodes
  in their files today.  1.4's non-promptable document rows are this
  lesson applied.
- **Formulas without programs.**  SolidWorks equations, Fusion
  parameters, upstream FreeCAD's expressions: safe because weak, which
  this fork's engine already outgrew (lambdas, assignments, callables
  in `ExpressionParser.y`).

The shape the prior art supports: FeatureScript's model, `LAMBDA`'s
carrier, OpenSCAD's discipline of no I/O, a versioned surface, and no
prompt that hands a document the user's authority.  Nothing on the
list needed the GUI in the box.

### 1.6 Audit of the abandoned rungs: what they touched, and the cut line **[recorded 2026-09-08; NOT executed]**

Asked 2026-09-08, before item 1 of sec 11: "do an audit for potential
reverting of touched code of abandoned rungs; count the frozen files;
the aim is trackable maintenance; decide whether to drop them" -- then
"I may later consider GUI access in expression, take that into
consideration" -- then "stop here, just document everything".  So this
section is the audit and the cut line; nothing was deleted.  Counts
are lines the branch ADDED since it forked from `origin/LinkVibe`
(merge base `2dd54590d8`, 120 commits).

**What the branch added, by fate:**

    bucket                                       files   lines   fate
    -------------------------------------------  -----  ------   ------------------------------
    expression sandbox core, gtests, corpus rig     68   24.8k   keep: the target's machinery
    permissions panel, padlock                       2    0.7k   keep
    host widget layer src/Gui/Fw/, fwuic,           16   11.1k   keep: the browser toolkit, in
      host freecad.widgets                                         native use (Pad, Pocket,
                                                                   TaskOrientation)
    guest widget models and shims                   28    8.9k   keep, dormant: the toolkit's
      (models.py 3.9k, items.py 2.9k, qtdata,                      Python half, item 5's premise
      uic, PySide/*, comm, IPython stand-in)                       and the FORM PATH below
    native fixes that fell out                       8    0.5k   keep: status bar registry,
                                                                   ToolBarManager, UiLoader,
                                                                   refreshLiveLoad, MeshLevelSource
                                                                   shutdown, Type::importModule,
                                                                   TechDraw crazy-edge, an Action
                                                                   use-after-free guard
    docs                                            11   11.6k   keep
    SandboxGui.cpp/.h, the gui.* op dispatcher       2    2.2k   SPLIT (below)
    guest FreeCADGui module (widgets/gui.py)         1    1.0k   SPLIT (below)
    GUI gates and probes                            14    5.9k   SPLIT (below)
    pivy and Coin in the guest                       1    0.1k   drop candidate (+ one option
                                                                   commit in the coin fork,
                                                                   COIN_BUILD_GL_STUB)

Everything in the last four rows is NEW files: deleting them is a
clean revert with no merge exposure.  `ExpressionDocumentOps.cpp` is
Kvedalen's expression code split out, not S1 work; the `Action.cpp`
hunk is a real use-after-free guard (a group's action of a deleted
command): both stay.

**The entanglement -- shared files edited by abandoned commits.**  21
files.  Eleven are the native fixes or the padlock and stay.  The
rest, the whole revert list outside the new files:

- `src/Gui/FreeCADGuiInit.py`: the InitGui runner, +124, one hunk.
- `src/Gui/Application.cpp`: the guest boot listener and the workbench
  wrapper wiring, part of +24.
- `src/Gui/CMakeLists.txt`, `src/Mod/Test/CMakeLists.txt`,
  `src/Mod/Test/InitGui.py`: file and gate lists.
- `src/Mod/Draft/CMakeLists.txt`, `src/Mod/BIM/CMakeLists.txt`: the
  GUI modules in the wheel file lists (the App-side packing is G1's
  and stays; the panel modules serve the form gates and stay).
- `src/Ext/freecad/CMakeLists.txt`, `InitializeFreeCADBuildOptions.
  cmake`, `src/App/CMakeLists.txt`: the ipywidgets/traitlets bundling
  of Probe B -- STAYS, it is the comm base of the form path.
- `src/Base/Type.cpp`/`.h`: a G3a hunk beside the kept import
  restriction (the form path's class lookup) -- stays.

Inside the kept core the guest GUI leaves a grep-able residue, about
400 lines in six files, each removable as a hunk: ~100 lines of guest
`FreeCADGui` prelude in `ImageMarshal.cpp`; the command, workbench,
comm and panel hook names in `ExpressionGuestProxy.cpp`; the catalog
rows `gui`, `gui.doCommand`, `app.write`, `prefs.write` in
`ExpressionSecurity.cpp`; the S1 `app.*` document ops in the bridge
(`reachable` itself is G1's and stays); the compiled-wheel loading for
pivy in `ExpressionPyodide*.cpp`.

**The cut line: the FORM PATH is kept, the WORKBENCH PATH is dropped.**
Ruled into consideration 2026-09-08: GUI access from an expression may
come later.  A document program with GUI access needs the form path --
a panel or dialog built from the guest as models, shown by the host,
its inputs returned, a selection read, a preference -- and never the
workbench path: commands, workbenches, the main window, the status bar,
docks, timers, the session document, `doCommand`, InitGui in the
guest, the 3D view.  Measured:

    piece                          form path, KEEP                 workbench path, DROP
    -----------------------------  ------------------------------  ------------------------------
    SandboxGui.cpp (2.1k)          comm, control, dialog, ui, sel, cmd, wb, mainwindow, docommand,
                                   menu, widget ops ~930 lines,    timer, doc ops ~900 lines
                                   plus ~290 shared
    guest gui.py (1.0k)            Control, Selection,             MainWindow shim, Command,
                                   SelectionObject, UiLoader, uic, GuiDocument, ActiveView, the
                                   user input, hints ~400          tool bar and status bar shims,
                                                                   the InitGui runner, MDI ~600
    guest models and shims (8.9k)  all                             none
    ipywidgets bundling (Probe B)  keep, the comm base             none
    gates                          SandboxWidgets, SandboxForms,   SandboxGui, SandboxInitGui,
                                   SandboxPanels, SandboxSelection, SandboxDraftGui,
                                   SandboxNative: 1.9k             SandboxSessionDoc,
                                                                   SandboxCorpusGui: 2.3k
    catalog rows                   gui, prefs.write stay, DENY     gui.doCommand, app.write go
                                   non-promptable for documents
                                   until ruled
    core residue                   comm and panel hook names, the  command and workbench hook
                                   guest module's form names       names, the S1 app.* ops, pivy
                                                                   wheel loading
    shared hunks                   Type.cpp's G3a hunk, the        FreeCADGuiInit.py runner, the
                                   Draft/BIM wheel lists           boot listener and workbench
                                                                   wrapper in Gui/Application.cpp,
                                                                   the gate lists in the Test
                                                                   CMake and InitGui.py
    pivy and Coin in the guest     none                            drop; a preview drawn by a
                                                                   document program would be the
                                                                   mirror, and 7.16 stays as
                                                                   its sizing

Net: about 4.2k lines dropped; about 5k lines of form path kept alive
with the 8.9k of models, exercised by the five form gates.  Draft's
and BIM's panels in `SandboxForms` and `SandboxPanels` become TOOLKIT
COVERAGE rather than a workbench goal, with the retire-on-break rule
per case.  The recommendation was DROP along this line rather than
freeze: freezing keeps 9.2k lines, 14 gates and the shared hunks alive
with no consumer, and every upstream Draft or BIM sync would still have
to decide what to do with them; dropping costs one commit and leaves
the door in git history and in the sec 7 sizings.  The execution, when
ruled: one commit that deletes the workbench path, trims the two files
and the residue, rebuilds, runs the expression gtests, the corpus gate
and the five form gates, and updates sections 0, 7 and 11.  **Status:
RULED 2026-09-09: "freeze everything" -- neither path is dropped; the
guest-GUI code stays FROZEN in place as sec 7's header says, gates
retired per case when an upstream sync breaks one.  The cut line above
is kept as the record for the day the ruling changes.  What the frozen
workbench path would still need to be fully working is evaluated in
1.7.**

**Two notes for the day GUI access from expressions is ruled**, so the
kept form path has a stated purpose:

- **Authority is the line, not the widget.**  A document-shown form
  grants nothing -- it collects inputs -- so it can be a document row
  without repeating the VBA mistake (1.5).  The risk is SPOOFING: a
  document dialog imitating FreeCAD's own prompts.  The host frames
  document-owned panels visibly (title and border); the form models
  already carry their principal.  The row would be narrower than
  `gui`: a `gui.form`-shaped permission for showing a panel and
  reading its inputs, nothing that reaches commands or documents.
- **Selection is document data.**  A document program's selection read
  is reach-checked like everything else -- its own document's objects
  only -- which the existing `reachable` rule (7.13) gives for free.

### 1.7 The workbench path: what fully working would still take **[evaluated 2026-09-09]**

Asked 2026-09-09, with the freeze ruling: "do an evaluation on what
else need to be done to actually make the workbench path fully
working".  "Fully working" is read as the retired rung 4: Draft and
BIM installed as workbenches whose Python runs in the guest, every
command usable from the tool bar, objects drawn, the workbenches'
own test suites green with the InitGui runner ON -- the switch the
2026-09-08 re-aim dropped.  What is built gets a workbench as far as
`Activated()` returning (7.9, 7.13, 7.15) with every Creator's
`IsActive` False and nothing drawn from the guest.  The remainder,
in dependency order; sizes are the sizings' where one exists.

    item                                  sized   size                 what it unblocks
    ------------------------------------  ------  -------------------  -------------------------------------
    G4a the mirror and the drawing loop   7.16    the larger half of   Draft_Line end to end; every Draft
                                                  G4: ~1400 host C++,  Creator's IsActive (get_3d_view);
                                                  250 guest C++ (a     every BIM command's poll; the
                                                  pivy rebuild), 900   camera, the view events, the MDI
                                                  wheel, 600 gate      observer, getActiveWindow (94 sites)
    G4b view providers in the guest       7.16    the smaller half     BIM STRICT in the corpus gate (the 3
                                                                       BuildingParts, Layer); the Views
                                                                       tree's icons; Arch.makeSite's terrain;
                                                                       closes the GuiUp cost list of sec 13
    G4c the residue                       7.16    measured after a, b  Draft_Edit's pick, host-node field
                                                                       writes, whatever the corpus hits next
    F1 the file and code chokepoints      7.14    an afternoon to a    a SESSION or ADDON guest stops
                                                  day                  reaching host files and host Python
                                                                       through Gui.runCommand (Std_Recent*,
                                                                       Std_DlgMacroExecuteDirect);
                                                                       mergeProject, importIFC.insert(path)
    the addon principal for guest         sec 13  unsized; a design    every registration from the guest runs
      workbench code                              question first       as `session` today -- a workbench's
                                                                       own grants, and a stand-in carrying
                                                                       them into the hooks the host calls
                                                                       later; without it "sandboxed
                                                                       workbench" grants nothing narrower
                                                                       than the session
    the stub list                         sec 13  many small items     preference-change notifications to
                                                                       the guest (Draft's tray and grid do
                                                                       not refresh); FreeCADGui.
                                                                       addDocumentObserver (BimSelect);
                                                                       FreeCAD.isRestoring(); the property
                                                                       editor's corner buttons; the host
                                                                       Model dock for tabifyDockWidget;
                                                                       sizeHint/QFontMetrics estimates;
                                                                       QTimer semantics; a qrc from the guest
    nativeifc / ifcopenshell in the guest 7.6     UNBOUNDED: a wasm    BIM's IFC half -- ArchSchedule's IFC
                                                  build of an OCCT-    branch, the IFC status widgets,
                                                  bound library, its   ifc_viewproviders, every native-IFC
                                                  own OCCT inside the  command; today a shim answers and
                                                  guest; unmeasured    the IFC workflow is host-only
    G5 the snapper in C++                 sec 11  when measured        every mouse move crosses once under
                                                                       G4a; the snapper's cost per move is
                                                                       the number to measure first
    P2 in-place install                   sec 11  a probe              a wheel with compiled extensions into
                                                                       a running guest (pivy today rides
                                                                       the image)
    G6 the suites                         sec 11  driven by failures,  Draft's and BIM's OWN test suites
                                                  unknown count        green with InitGui in the guest --
                                                                       the only gate that means "fully
                                                                       working"; the corpus gates cover
                                                                       recompute, not the commands

Beyond Draft and BIM, "everything Python in pyodide" is not one more
step but the same ladder again per workbench, each a G1 (App side,
7.6) plus a G2/G3 (GUI side) with its own losses: FEM's solvers run
external processes (host.exec, F1's row), CAM's post processors write
files (fs.write) and its tool library reads them, Spreadsheet's and
TechDraw's Python, PartDesign's and Sketcher's Python helpers, the
Addon Manager (network, N1-N4).  None is sized.

Two structural notes that no item above removes:

- **The mirror is Coin-shaped and desktop-shaped.**  7.16 walks guest
  `SoNode`s into the host's Coin scene.  The browser tier has no Coin:
  the workbench path in the browser would need a second reader over
  the renderer's scene (SceneServer, `docs/SceneStreaming.md`) and G7's
  DOM walker for the panels.  Fully working on the desktop is not
  fully working across the tiers the project is aimed at (CLAUDE.md).
- **Every upstream Draft or BIM sync re-runs the port.**  The wheels
  carry Draft's and BIM's code unmodified, so a sync costs nothing in
  the wheels, but the corpus and command gates re-measure what the
  sync's new imports and new `FreeCADGui` uses broke -- the
  retire-on-break rule is the freeze's standing cost, and it grows
  with every item above that is built.

Rough scale at the pace of sec 7 (7.15 was a day for a sized item,
G3 four stages in two days): G4 two to four sessions, F1 one, the
addon principal a sizing plus one, the stub list spread over the gates
that need each stub, G6 unknown until run, ifcopenshell not
estimable.  A fully working workbench path for Draft alone is the
first four rows plus G6; for BIM the same plus the IFC row, which is
the one that may not be reachable.  Verdict: the path is about half
built by line count and less than half by capability; the largest
unbuilt piece (G4) is fully sized and the largest unknown
(ifcopenshell) is outside this project's code.  None of it is started
under the freeze.

## 2. The security model **[built]**

Modeled on browser origins: wasm guest = sandboxed renderer, host
dispatcher = browser kernel, principal = origin, grant flow = permission
prompt, per-document panel = site settings, PROMPT scoping =
popup-blocker (once / session / always), quotas = tab throttling.

### 2.1 Principals

Three classes (`ExpressionSecurity.h`, `PrincipalClass`):
`document:sha256:<hex>`, `session`, `addon:<name>`.  The C++ core is
not a principal.  A document's identity is content-addressed:
`DocumentHashBuilder` collects every expression, cell and script code
string into a sorted set, joins them with `\x1F` under the prefix
`"fcexpr-v1\0"`, and SHA-256s the result.  Tampering with a trusted
file voids its grants; copying a trusted file's Uid into a hostile one
gains nothing.  Console and script evaluation run as `session`
(`DocumentObjectPyImp.cpp:373` pushes a `Runtime::Scope("session")`).

### 2.2 The catalog (frozen v1, plus one action)

`catalogDefault()` in `ExpressionSecurity.cpp`; addons default to ALLOW
across the board.

    permission        document   session   addon   notes
    ----------------  --------   -------   -----   -------------------------------
    doc.read.self     ALLOW      ALLOW     ALLOW   same-origin rule
    doc.write.self    ALLOW      ALLOW     ALLOW   write_prop + write family, the owner's
                                                   DOCUMENT: self = same origin (3.2; user
                                                   ruling 2026-09-05, owner-only retired)
    doc.foreign       PROMPT     ALLOW     ALLOW   the cross-origin wall
    geom.call         ALLOW      ALLOW     ALLOW   cost bounded by budget, not grant
    app.query         PROMPT     ALLOW     ALLOW
    prefs.read        ALLOW      ALLOW     ALLOW   the parameter store, read only, through a
                                                   curated reader (draftutils.params); added
                                                   2026-09-04, user ruling (7.6 G1c decision 2)
    prefs.write       DENY (np)  ALLOW     ALLOW   the parameter store, written through the
                                                   same facade (draftutils.params.set_param,
                                                   ParamGet(...).Set*): a task panel storing
                                                   the user's choice; added 2026-09-06 (7.11)
    gui               DENY (np)  ALLOW     ALLOW   non-promptable for a document, as prefs.write
    host.import:<m>   PROMPT     PROMPT    ALLOW   per module
    unsafe.getattr    DENY       PROMPT    ALLOW   host-side Python attribute walks
    pkg.install:<p>   PROMPT     PROMPT    PROMPT  an ACTION, never a grant (sec 5.5)
    gui.doCommand     DENY (np)  ALLOW     PROMPT  Gui.doCommand / doCommandGui / addModule from
                                                   the guest: the source runs IN the guest, the
                                                   host records the macro line and an audit line
                                                   (the source's sha256); the one row an addon
                                                   is prompted for; built 2026-09-07 (7.13, S2)
    app.write         DENY (np)  ALLOW     ALLOW   newDocument/closeDocument/setActiveDocument
                                                   (FcxWire app.new_doc/close_doc/set_active_doc);
                                                   ruled and built 2026-09-07 (7.13, S1)
    fs.read:<path>    DENY (np)  PROMPT    PROMPT  a host file read at the core's chokepoints
    fs.write:<path>   DENY (np)  PROMPT    PROMPT  (Gui::Application::open/importFrom/exportTo,
    host.exec:<path>  DENY (np)  PROMPT    PROMPT  openDocument, saveAs, MacroManager::run,
                                                   Interpreter::runFile), a picker-blessed path
                                                   passing; designed (7.14), not in the enum yet
    net.*             --         --        --      designed (sec 6), not in the enum yet

The one deliberate compatibility break: `unsafe.getattr` (the
`obj.Proxy.foo` drill-down through a live host object) is DENY for
documents by design.  The corpus (sec 8.3) has zero such uses.
`host.import` classifies modules: `math`, `re`, `_sre`, `collections`,
`builtins` are free (ring 0); `FreeCAD`/`App` is `app.query`;
`FreeCADGui`/`Gui` is `gui`; everything else is
`host.import:<name>` (`ExpressionSecurityRuntime.h:342-344`).

### 2.3 Grants

- Scopes: once, session, always.  `PermissionNeeded` fails fast; a
  recompute is never blocked on a prompt.  A denial crosses the wire as
  a plain `Base::RuntimeError`, not `PermissionNeededException` (tests
  rely on the difference).
- Store: `<UserAppData>/security/grants.json` (schema v1) with a sibling
  `audit.log`; `GrantStore`, `AuditLog` in `ExpressionSecurity.h`.
- Headless: `--grant <permission>[:<target>]` (repeatable) and
  `--policy <file>` (a grants.json used instead of the user store),
  `Application.cpp:2586-2587`.
- Preference `Expression/Security:Enforce` (default true) switches
  enforcement off for rigs that measure parity, not policy.
- Python: `FreeCAD.ExpressionSecurity` -- `principal`, `grant`, `revoke`,
  `grants`, `pending`, `principalOf`, `resolve`, `clearOnce`,
  `clearPending`, `enforced`.

### 2.4 Enforcement point

Host-side only, never in the guest: `ExpressionSecurityRuntime.h`
compiles every `check*` to a no-op under `FC_EXPR_IMAGE`, so the guest
cannot contain enforcement even by mistake.  The permission service was
wired into the old chokepoints rather than replacing them:
`ImportModules::getModule` calls `checkModuleImport`
(`Expression.cpp:2375`), `CallableExpression::securityCheck` keeps the
eight-name denylist and adds `checkCallablePermission`
(`Expression.cpp:4490-4530`), `Component::get` calls `checkGetattr`
right above the still-commented-out module check it supersedes
(`ObjectIdentifier.cpp:653-671`).  `getvar`/`hasvar` and
`Base::Interpreter().getVariable` were deleted, not disabled.  Every op
over the wire carries its principal and passes the schema check, then
the permission check, on the host.

### 2.5 The panel and the padlock **[built 2026-08-30/31]**

`Gui::Dialog::DlgDocumentPermissions` (591 lines): per-document grants
with the four verbs (grant, deny, inspect, revoke), once/session/always
scopes, a `PermissionIndicator` and a `SandboxIndicator` status-bar
padlock whose click toggles routing, warns when routing is on with no
runtime present, and offers the pyodide runtime download when none is
installed.  A `pkg.install:<name>` row's allow button runs the
installer (sec 5.5).  Any roadmap text that still lists "the
permissions panel" as future work is stale; the remaining panel work is
adding rows for the network permissions.

## 3. Architecture **[built]**

### 3.1 The boundary

Architecture B: the Python value layer moves into the sandbox; OCCT and
the Document stay host-side.  Architecture A (a C++ value path that
de-Pythonizes arithmetic) was measured NOT to be a prerequisite (sec
8.1).  The AST walker itself is compiled into the image ("image-walk";
"host-walk", one crossing per operator, was rejected).

`ExpressionCore` (`Expression.cpp`, `ObjectIdentifier.cpp`, `Range.cpp`)
compiles into the image behind `FC_EXPR_IMAGE` against an adapter world
(`ExpressionImage/FcxDocument.{h,cpp}`) that keeps the host's class
names (`App::Document`, `DocumentObject`, `Property*`,
`PropertyContainer`, `PropertyLinkBase`, about fifteen methods) so
`ObjectIdentifier.h` needs no change.  Host-only code -- the security
chokepoints, `DocumentObjectPy`/`ExpressionPy` type checks, the
maintenance overrides of `VariableExpression`/`RangeExpression` -- is
compiled out.  One source list, `ExpressionImage/ImageSources.cmake`,
feeds both guests (the WASI reactor and the pyodide extension module).

Three rings: ring 0 is in-image native (CPython, the core, Base math
bindings, a stdlib slice); ring 1 is generated facades for host types;
ring 2 is absent (`os`, `socket`, `ctypes`: no grant supplies them).

### 3.2 The wire (`FcxWire.h`)

Ops: `eval`, `exec`, `read_prop`, `get_attr`, `call`, `get_item`, `len`,
`bool`, `str`, `release`, `resolve_alias`, `pkg.missing`, `write_prop`,
`mod_call`, `mod_get`; and the `gui.*` family (2026-09-05, 7.9) --
`gui.cmd.add`, `gui.cmd.list`, `gui.wb.add`, `gui.wb.remove`, `gui.wb`,
`gui.wb.active`, `gui.wb.list`, `gui.icon_path`, `gui.lang_path`,
`gui.pref_page` -- which `App` does not answer: the bridge has an op
REGISTRY (`registerBridgeOps(prefix, handler)`, matched after the
built-in ops, inside the same GIL and exception net; request and reply
cross that seam as CBOR bytes, since a json in a signature does not link
between the two nlohmann copies, 12) and `Gui` registers
the family when its `Application` is constructed (`SandboxGui.cpp`),
every op checked against the catalog's `gui` permission.  `bool` and `str` (2026-09-04) are the proxy's
`__bool__` and `__str__`: natively every object is truthy unless its
type says otherwise (`if plane:` -- with only `__len__` on the proxy the
host answered "no len()" and Draft's plane tests died), and Draft
compares curves by `str()`; both are answered by the C type's own slot,
and a heap type (a Python-defined `__bool__`/`__str__` is host code)
rides the unsafe gate.  Two more wire rules the draftgeoutils gate
forced: a module facade's class object crosses as a type reference
(`{"t":"ty","q":"Part.Edge"}`, decoded on the host to the declared
object -- `shape.ancestorsOfType(v, Part.Edge)`), and this fork's
`Part.ShapeList` (what `Shape.Edges` returns, a sequence type of its
own) crosses as a plain list of handles, the classic shape Draft's
slicing and concatenation expect.  The module facades (2026-09-04, `MODULE_FACADES` in the
generator, sec 3.3): the guest's `Part` module is a closed list of
names -- 30 callables (`LineSegment`, `makePolygon`, `Face`, ...), one
constant (`OCC_VERSION`), one exception (`OCCError`) -- built at init
into the guest's `sys.modules`; a callable is `{op:"mod_call", m:
"Part.LineSegment", a, k}` run on the host with handle arguments
dereferenced, its result crossing by value or as a handle with the
annotated facade above it; a constant is read once over `mod_get`; the
exception is a guest-local class the bridge raises whenever a host
reply names it (`raiseFromReply` consults the facades' `EXCEPTIONS`
after the builtins).  Each module's table row names the catalog
permission its ops are checked against (`ModuleMember::permission`,
2026-09-04): `Part` is `geom.call`, a decision -- a curated constructor
list is a geometry call, where `_part` (an arbitrary import) is
`host.import`; `draftutils.params` (`get_param`, `get_param_view`,
the two names Draft's App side reads preferences through) is
`prefs.read` (2.2; `app.query` until 2026-09-04, when a document
object's `Wire.__init__` would have prompted for `MakeFaceMode`),
answered by the host's own
Draft reader by value -- the bundled wheel (5.6) leaves
`draftutils/params.py` out, and the facade sits in `sys.modules` before
the wheel's `draftutils` package is imported, so `from draftutils
import params` finds it.  An undeclared name is not on the guest
module at all (AttributeError), and a forged `mod_call` is a protocol
error.  `exec` (2026-09-04,
host->guest, `ImageHost::exec(source, module)`) runs statements; with a
module name the source becomes that module in the guest's `sys.modules`
(created and registered before it runs, bound to its parent package
when dotted, removed on failure) -- how a test pushes a harness or a
source under study into the guest; workbench code itself boots as a
bundled wheel (5.6).  Value tags: `quantity`,
`vec`, `rot`, `pla`, `mat`, `bb`, `h` (handle), `tup` (a tuple crosses
as a tuple, the first corpus-gate finding).  Reply shape `{ok, val}` or
`{ok:false, exc, msg}`.  The one write op is `write_prop` (2026-09-04,
`{op, h, a: name, v: value}`, the proxy's `__setattr__`): the handle
must belong to the EVALUATION OWNER'S DOCUMENT -- the owner itself
(`HandleTable::owner()`, set by `evalExpression` and by a raw `eval`
that names one), any object of its document, or that document -- under
`doc.write.self`, then the C++ property system's `setPyObject` (typed;
a shape re-maps its element map), never host Python.  "Self" is the
same-origin document, as 2.2 defines the principal: a document
rewriting its own objects is native behaviour (ArchStairs sets its
railings' `Base`, a PipeConnector its pipes' offsets, a Schedule its
Result sheet's cells), and the wall that matters is `doc.foreign`, an
object of another document.  Until 2026-09-05 the gate was OWNER ONLY
-- rung 2's "an execute() writes self" scoping, encoded as policy; the
user ruled it retired ("I thought it means an object can write anything
inside its own document"), and with it `Document.addObject`/
`removeObject` on the owner's own document are declared under the same
permission (7.6 decision 1 revised).  `Immutable` refuses, as native `setattr` does; `ReadOnly`
is the editor's status and writes through it succeed natively, so they
do here too (the gate is parity, not extra policy).  The value
decodes through the handle table, so a host object crossing back as a
handle dereferences to the live object (a `PropertyLink` takes the
object, not its wire face).  `addProperty`, `removeProperty`,
`setPropertyStatus` are declared `call` members (DocumentObjectPy.xml,
PropertyContainerPy.xml -- the facade chain now reaches
PropertyContainer through ExtensionContainer) and ride the `call` op
behind the same same-document gate (so do `configLinkProperty` and
`setLink` of the Link extension, G1d; `Document.addObject`/
`removeObject`; and the Sheet's cell writes `set`, `clear`, `clearAll`,
`mergeCells`, `splitCell`, `setStyle`, `setAlignment`, `setForeground`,
`setBackground`, `setColumnWidth`, `setRowHeight`, `setAlias`,
`setDisplayUnit`, `insert/removeRows/Columns`, `touchCells`,
`recomputeCells` -- Schedule and Report fill their Result, 2026-09-05).  A `write_prop` on a handle
that is NOT a property container -- a value object the transaction
holds: a shape the guest built, a property's copy -- is no document
write: it takes a DECLARED attribute of the type (the same closed
table as `get_attr`) through the type's own setter under `geom.call`,
a read-only attribute refusing natively (G1d, 2026-09-04: Draft's
WorkingPlaneProxy sets `Placement` on the plane it made).  Refusals are `PermissionError`, not
`ProtocolError`: the request is well-formed, the principal is not
allowed.  The read path never enters host Python
(`read_prop` is answered from the C++ property system); host CPython
runs only for members annotated `call`.  Arguments validate against the
XML-declared signature; a sandbox callable is never a valid argument.
Rung 2 (2026-09-04, G1c, sec 7.6): two more host->guest ops and one
value.  `proxy_new {mod, cls, a, alloc?}` imports a class in the guest
and constructs it -- `obj.Proxy = self` inside `__init__` is caught by
the proxy's `__setattr__`, which registers the instance in the guest's
registry and sends `write_prop Proxy` with the descriptor
`{"t":"gproxy", id, mod, cls, hooks}`; the host decodes that into the
STAND-IN (`ExpressionGuestProxy.h`), one per guest proxy, and the
Proxy property holds it.  `proxy_call {id, m, a, k?}` runs hook `m` of
the registered instance with the decoded arguments (the object rides
as a handle) and returns the result by value; the stand-in's hook
attributes are forwarders that do exactly this, and the OWNER of the
call is the document object in the first argument, so the hook may
write it.  `gproxy` also crosses host->guest as `{"t":"gproxy", id}`
(an `obj.Proxy` read resolves to the guest instance) and any
host->guest request may carry `"pd":[ids]`, stand-ins that died.  A
handle whose object carries extensions names their facades in `"ext"`
(`Part.AttachExtension` on a Draft Wire); the guest composes the proxy
class from the type's facade plus theirs, and the host looks a member
up on the container's extension types after the type's own MRO, since
extension methods are injected per instance and never appear in it.
Round trips NEST: a guest hook's `write_prop` runs the host's
`onChanged`, whose hook is a `proxy_call` inside the pending bridge op
(`ImageHost::Private::Transaction` keeps a depth, restores the outer
owner, flushes releases and performs a requested reset only at the
outermost level; wasmtime enters the store through the CALLER's
context while nested, the pyodide runtime arms the budget once).
G1d (2026-09-04) added two host->guest ops and one value for the
HOST's side of a Proxy: `proxy_get {id, n}` reads attribute `n` of the
registered instance -- data comes back by value, a callable as
`{"t":"gmethod", id, n}`, which decodes to a forwarder bound like a
hook (calling it is a `proxy_call` with the object argument as owner);
`proxy_set {id, n, v}` writes a VALUE (a host object would cross as a
handle no transaction outlives, so the host refuses it with a
TypeError before the trip).  The stand-in's `getattr` answers its own
dict first (the hooks), refuses dunders and the hook names the
descriptor did not list without a trip, and sends everything else as
`proxy_get`, keeping a method it got back on the stand-in; its
`setattr` is `proxy_set`.  `proxy_new` also takes `k` (kwargs): the
construction dispatch (7.6, G1d) sends a class's real call.
Three more guest->host ops came with BIM (G1d): `ext {h}` re-reads a
handle's extension facades after the guest called `addExtension` on
it (the proxy recomposes its class); `active_doc` answers the guest's
`FreeCAD.ActiveDocument` with the transaction owner's `Document`
through `get_attr` on the owner's own handle (None outside a hook);
`pkg.missing` is unchanged.  And the WRITE-BACK: every value a handle
proxy hands out (`read_prop`, `get_attr`) is stamped as that
attribute of the proxy (`PyObjectBase::trackAttributeOf`, the
prelude's `_fcx.track`), so a nested write -- `obj.Placement.Base =
v`, ArchFrame's `profile.Placement.Rotation = rot` -- writes the whole
value back through the proxy's `__setattr__` = `write_prop`, exactly
the write-back native FreeCAD performs for every PyObjectBase an
attribute returns (`startNotify`, which now takes a generic setattr
for a parent that is not a PyObjectBase).  Before it, such a write
was silently lost in the guest and the Arch Frame's profiles never
turned.  Two more identity rules from the same corpus: the host mints
ONE handle id per object for the life of a transaction (a use count
per id, the entry going with the last release), and a proxy compares
and hashes by id, so `obj in o.Hosts` and `a == b` hold as they do
natively (ArchComponent found no window to subtract from its wall
before); and `touch` on any object of the OWNER'S document (ArchWindow
touches its host wall from its own execute so the wall subtracts the
opening in the same recompute) was the first same-document write
allowed -- the special case the 2026-09-05 ruling made the rule for
every write.

The fixed layout (2026-09-04, step 7 of the coding order): a bare
`read_prop`/`get_attr` with a name -- 2738 of the 3575 hops in the
draftgeoutils gate -- skips CBOR on both sides.  Request `0xF1`, op
byte, handle u64, name (u16 length + UTF-8); reply `0xF2`, kind byte,
then a float64, a bool, an int64, a string or a Vector inline -- or
kind 0 and the CBOR reply as before for any other value and every
error.  The guest takes it when no releases are queued (those ride
"r" on a CBOR request); the host recognises the magic byte, which no
CBOR document of this wire begins with.  Everything else is CBOR
exactly as before; the two forms share the dispatcher.

Handles are transaction-scoped and decoded BEFORE `clearHandles()`.
Releases never cross as an op (2026-09-04, step 6 of the coding
order): a proxy's `__del__` queues its id in the guest, and the queue
rides as `"r"` on the next guest->host request or on the evaluation's
own reply -- the host releases them before the op, or after the trip,
under the deferral below.  Before this, 58 percent of all hops in the
draftgeoutils gate were single releases (4977 of ~8550 for 116 calls).
Releases are deferred for one transaction and applied at the start of
the next: the spreadsheet idiom `tuple(.cells, <<B4>>, <<ZZ4>>)` returns
a tuple whose first element IS a host object, and the image destroys
its proxies as the evaluation unwinds -- before the host decodes the
reply -- so an eager release turned the handle into a stale id.  The
flush DECREFs under the GIL, only when an interpreter exists
(`e2969a8503`; this paragraph existed only in that commit message
before).

Durable document-object handles (2026-09-05, 7.6 "G1d CLOSED"): an id
lives one transaction, but a Proxy keeps document objects on itself
across hooks natively -- ArchReport's `self.spreadsheet`, the `self.obj
= obj` of several `onDocumentRestored`s -- and the table that minted
the id is cleared by every expression evaluation in between (a
Schedule's Result sheet has a numeric cell, and a numeric cell is an
expression), so the second routed recompute of the BIM corpus found
ArchReport's cached sheet stale.  Every handle to a DocumentObject or
Document now carries its key in `"k"`: `[document, name]`, or
`[document]` for the Document itself (the host's `encodeHostValue`, the
guest's `getPyObject` for the expression owner).  The guest proxy keeps
it in `_k`, compares and hashes by key when both sides have one (so a
cached object equals a fresh handle to it, as natively), and sends
every op through `_hop`: on a `ReferenceError` from a stale id it asks
`resolve {a: key}` once and retries with the fresh id the host replies
(an integer; one use minted for the caller, the proxy keeps its class).
A cached handle that arrives as an ARGUMENT -- `obj.Ref = self.other`,
a PropertyLink taking a kept object -- re-resolves the same way inside
the host's decode, no op of its own.  The reach of a key is exactly
what the guest already has: the evaluation owner's document under
`doc.read.self` (what `owner.Document.getObject(name)` gives); a key
into another document is a `PermissionError` (a key is names the guest
could make up, a handle a capability it was handed), a name no longer
in the document a `ReferenceError` (deleted natively too, the Proxy
holds a dead object).  A value object -- a shape, a curve -- has no
key and stays transaction-scoped (natively a shape kept across hooks is
a copy anyway).  Two more from the same fix: `clearHandles()` is a no-op
while a round trip is nested (an expression an outer hook's write
recomputed would otherwise drop the hook's own live arguments), and the
corpus gates recompute TWICE, since one recompute never meets a cached
handle.  Gate: `guestHandlesDurableAcrossHooks` (both runtimes).

The bindings pack: identifiers are pre-resolved host-side
(`getIdentifiers()`/`getDeps()`) and shipped with the request as values
or handles, so a pack hit costs zero crossings; on a miss the real
`resolve()`/`access()` run against the adapter world.  Failures ship
too (`binderrs`), but only for identifiers naming a document other than
the owner's -- a blanket "ship every failure" would break in-eval bound
variables (`a = Width + 1; a * 2`).  The pack resolves the WHOLE
identifier host-side, which is why link placements, `getSubObject`
walks, `_shape`, `_self` and indexed group access evaluate identically
native and routed.

### 3.3 Generated facades

`src/Tools/bindings/generateSandboxFacades.py` reads
`<Sandbox tier="value|handle|call"/>` on the `*Py.xml` members and
emits ONE `FcxDispatch.inc` (host) and ONE `FcxFacades.inc` (a Python
source string for the image) from the same list, so the two guests
cannot drift.  Absent means DENY: a new binding is unreachable until
annotated, and the security review of the bridge is "diff the
annotations".  Until 2026-09-04 that was 14 members in five XMLs; G1
step 5 grew it to 239 members across 50 facades in 50 XMLs, the
Draft App-side surface of sec 7.6 (`--surface`): the nine TopoShape
bindings, Geometry/Curve/Surface and the concrete curves and surfaces
Draft touches, Geometry extensions, BaseClass (`TypeId`,
`isDerivedFrom`), Document/DocumentObject/PropertyContainer reads and
the write family.  Value classes (Vector, Rotation, Placement, Matrix,
BoundBox, Quantity) need no annotation: they cross by value and exist
in the guest.  The list lives ONLY in the generator (`ANNOTATED_XMLS`,
fathers first -- an XML with no annotation of its own, Persistence or
TrimmedCurve, is listed so the Father chain reaches through it); the
two CMake custom commands take their DEPENDS from `--list-xmls` so
nothing drifts.

Two traps the growth uncovered.  A facade's key must be the RUNTIME
`tp_name`, and Part renames all nine TopoShape types at module init
(`AppPart.cpp`: TopoShapePy becomes `Part.Shape`, then `Part.Edge`,
`Part.Solid`, ...), so the XML-derived `Part.TopoShape` never matched
a live shape and every shape fell through to `read_prop` -- that was
the caveat recorded in the crossing analysis, not a python-mode quirk;
`RUNTIME_TYPE_NAMES` in the generator carries the nine, and
`merge_same_key` folds any genuine collision into one facade (the
union of members; a member the object lacks raises AttributeError
from the host as natively).  And a facade is verified only by real
guest Python on a real object
(`ExpressionImageEvalTest.partSurfaceOnHandles`), never by reading
the XML.

7.17 D1 (2026-09-13) grew the set to 362 members across 57 facades --
the solid constructors and shape operations a document program builds
with -- and VERSIONED it: `FCX_SURFACE_VERSION` in the generator, bumped
by hand when a member is removed or changes meaning, and the sha256 of
the annotated set beside it, both generated into `FcxDispatch.inc` and
`FcxFacades.inc`; `surfaceVersion(True)` compares the guest's copy with
the host's (7.17 D1).

### 3.4 The evaluation seam

`src/App/ExpressionEvaluator.{h,cpp}` is the single seam, deliberately
NOT in `ExpressionCore` and deliberately at the CALLERS rather than
inside `getPyValue` (the 12x hop cost makes per-node routing absurd).
Re-entrancy during an image evaluation runs natively (thread-local
guard); the AST is not re-parsed host-side.  `SandboxStatus` /
`sandboxStatus()` (`ExpressionEvaluator.h:72-96`) is a cheap preference
read plus two stats; `confined()` is host built, image present, routing
enabled.  `ImageHost::evalCount()` counts crossings; `ImageHost::stats()`
(2026-09-04) counts them by kind -- evaluations, handles minted, and
every guest->host op by wire name -- with `resetStats()` to zero them.
That is the instrument for G1a: run a Draft or Arch `execute()` in the
guest, read `FreeCAD.ExpressionSandbox.stats()['ops']`, and the
read_prop/get_attr/call counts say where a snapshot op would pay
(sec 8.1) before one is designed.

### 3.5 The router: what is routed

Preference `Expression/Sandbox:Evaluate`, **ON by default since
2026-09-16** (OFF by default before).  Routed:
expression evaluation, the spreadsheet through the single seam
`PropertySheet::eval` (and its `evalPy` twin) -- python mode CROSSES
rather than being refused, since python mode is a lexer state plus
builtins visibility, both in the image -- with `OptionCallFrame` /
`OptionPythonMode` crossing in the request; paste-as-value,
`setContent`, `EditQuantity`, `Cell::getPyValue` in EditNormal,
`DlgSheetConf` range validation.  NOT routed: the GUI live expression
editors (`DlgExpressionInput`, `SpinBox`, `InputField`, `PropertyItem`)
-- policy-gated as session, not confined; a named future slice.
Writes were never the router's problem: `PropertyExpressionEngine::
execute` sets the path on the host after the value returns.
The same preference routes a document object's saved Proxy at document
OPEN (`ExpressionSandbox::proxyRestoreRouted()`: the preference AND
`ImageHost::available()`, since 2026-09-16 -- the paragraph below):
`PropertyPythonObject::Restore` builds the
`<Python module=".." class="..">` instance in the guest and holds the
stand-in (7.6 G1c step (f)); a module the guest cannot serve FAILS
CLOSED.  A view provider's Proxy is not routed -- the Gui side is not
in the guest (G2).

**The restore half asks `available()` too (2026-09-16).**  It used to
read the preference alone, deliberately, so that answering it booted no
image.  The cost of that was a build with the preference on and no
runtime INSTALLED: every saved Proxy took the routed path, no guest
could answer, the restore failed closed, and the document came back
de-Proxied -- 70 of 70 objects of `data/examples/draft_test_objects.FCStd`,
measured.  Evaluation never had the problem (`evaluationRouted()` has
always ended in `available()` and falls back to native).  `available()`
is memoized and, where there is no runtime, answers no without booting
anything; off the routed path the native restore still runs under the
Mod-root import restriction of sec 11 item 1, so refusing widens
nothing.  Gate: `ExpressionImageEvalTest.proxyRestoreNeedsRuntime`.

Python: `FreeCAD.ExpressionSandbox` -- `routed`, `setRouting`,
`available`, `imageInfo`, `evaluate`, `evaluateNative`, `exec` (statements
in the guest as the session principal, optionally as a named module),
`evalCount`, `stats`, `resetStats`, `reset`, `proxyNew`, `proxyInfo` (rung 2, 3.2),
`pyodideReleases`, `pyodideLayout`, `pyodideVerify`,
`pyodideAbi`; constants `OptionCallFrame`, `OptionPythonMode`.

## 4. Runtimes **[built]**

`ImageRuntime` (`ExpressionImageRuntime.h`) is the seam; two
implementations register by name.  Selection (`runtimeChoice()`,
`ExpressionImageHost.cpp:69-83`): preference `Expression/Sandbox:Runtime`,
else environment `FCX_RUNTIME`, else `"pyodide"` when the pyodide host
is compiled in (the normal build), else `"wasi"`.

### 4.1 pyodide on a bare V8 (the default) **[built 2026-09-02/03]**

Three pieces: the engine is `v8-embed`'s `libv8.so` (V8 14.6, built
out of node 26.6.0's tree by `realthunder/v8-embed-feedstock` as a bare
engine: no node modules, no default context, five conda platforms plus
Windows from source); the shim is `src/App/PyodideHost/host_shim.js`
(291 lines), the minimal d8-shape surface pyodide's loader needs --
`console`, `read`/`readbuffer`/`load`, `os.system` (a string-matched
RNG hook), `crypto.*`, `performance.now`, timers and `queueMicrotask`,
`TextEncoder`/`TextDecoder`, `URL`, `btoa`/`atob`, `self` -- and
NOTHING else: no `process`, `require`, `module`, `fs`, `fetch`,
`XMLHttpRequest`, `Worker`, `WebSocket`, `MessageChannel`; the native
surface is `PyodideRuntime` (`ExpressionPyodideRuntime.cpp`), which
installs `readText`/`readBytes`/`randomBytes`/`now`/`print` on
`__fcx_host` plus the dynamic-import and import-meta callbacks.
Confinement is by construction: what the guest can reach is what the
shim defines.  `PyodideProbe.cc` is the standalone probe binary only.

Boot and call: `pyodide_glue.js` provides `__fcx_boot(root, wheel,
packagesDir, names)`, `__fcx_link()`, `__fcx_setInterrupt`,
`__fcx_teardown`.  The transport (2026-09-04) is the wasi reactor's
own shape on both sides, with no JavaScript in the path: guest->host is
the pair of wasm imports `fcx_host_call(ptr, len) -> reply length` /
`fcx_host_fetch(dst, cap)` (ImageBridge.cpp, module `env` for the side
module, `fcx` for the reactor), installed as V8 natives and merged into
the main module's symbol table with `Module.mergeLibSymbols` BEFORE
the wheel loads, so the dynamic linker binds the side module's imports
to them; the native reads the request out of `Module.HEAPU8` in place.
Host->guest is the side module's `fcx_alloc`/`fcx_call`/`fcx_free`
exports (ImageModule.cpp, `EMSCRIPTEN_KEEPALIVE` plus
`-sEXPORTED_FUNCTIONS`), taken from `Module.LDSO.loadedLibsByName`
and called from C++ as wasm functions.  `HEAPU8` is re-read per use:
memory growth replaces it.  The earlier shapes -- a Python callable
returning bytes (a proxy per crossing, 40 us of floor), then two
bytearrays viewed through `PyProxy.getBuffer()` -- are gone, along with
`set_host`, `set_host_buffered`, `buffers`, `grow_request`,
`call_len` and `__fcx_call`.  The guest wheel
`fcx_image-<ver>-cp314-cp314-pyodide_2026_0_wasm32.whl` is
`_fcx_image`, the same `ImageSources.cmake` compiled by the pyodide
toolchain (emsdk 5.0.3, pyodide-build 0.39.0) with `-fPIC
-fwasm-exceptions -sSUPPORT_LONGJMP=wasm -fvisibility=hidden -flto`,
linked `-sSIDE_MODULE=2 -sWASM_BIGINT`; it must be an `add_executable`
with a `.so` suffix, not `add_library(MODULE)`, which CMake's Emscripten
platform would archive with `emar`.  `-fvisibility=hidden -flto` cut the
wheel 427 to 368 KB and the hop cost by a third.

The time budget (both runtimes): two stages, a soft interrupt through
CPython's emscripten signal handling on a shared `int32` buffer, then a
hard `Isolate::TerminateExecution` (wasmtime: epoch interruption);
`Expression/Sandbox:BudgetMs` 5000, `GraceMs` 1000; outcomes
`Interrupted` / `Terminated`; `Watchdog` in
`ExpressionPyodideRuntime.cpp:389-517`.  Budget overhead: WASI transport
floor 2.6 to 3.1 us; pyodide inside run-to-run noise.

Memory (7.17 D4, BUILT 2026-09-14): a ceiling per guest,
`Sandbox:MemoryMB` (1024) on its linear memory and array buffers and
`Sandbox:EngineHeapMB` (512) on V8's heap.  pyodide's memory is
EXPORTED by its module with a 4 GB maximum compiled in, so neither
`MAXIMUM_MEMORY` nor a constructor clamp reaches it; every growth goes
through emscripten's `growMemory` -> `WebAssembly.Memory.prototype.grow`,
which the shim wraps -- refused past the ceiling is a failed sbrk, a
`MemoryError` in Python, the guest kept (`Outcome::MemoryRefused`).
The engine heap cannot refuse: its near-limit callback stops the guest
(`Outcome::MemoryExhausted`, dropped).  After boot the guest has no
second memory: the shim refuses a module defining one and a memory made
from JavaScript (the compile gate).  Details and limits in 7.17 "D4,
BUILT".

### 4.2 The WASI image (the reference) **[built 2026-08-30/31, frozen]**

Built only with `BUILD_EXPR_WASI_RUNTIME` (default OFF), fixed when
broken, never extended.  Toolchain: wasi-sdk 33 (the floor:
exceptions-enabled libc++) to 34 (the ceiling for CPython's
`wasm32-wasi` target name), wasmtime v48.0.1 CLI and C API, CPython
3.12.13 from `Tools/wasm/wasm_build.py wasi`.  `ExpressionImage/
CMakeLists.txt` is a standalone cross project; `CMAKE_BUILD_TYPE`
defaults to Release -- an early build left it empty, ran -O0, and every
number taken before that fix was 3 to 6x too slow (sec 8.1).  Flags:
`-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false`,
`-mexec-model=reactor`, `-DFC_NO_QT`, `-DBOOST_DISABLE_THREADS
-DBOOST_SYSTEM_DISABLE_THREADS`.  Real in the image: Type, BaseClass,
Vector3D, Rotation, Matrix, Placement, DualQuaternion, Quantity, Unit,
Exception, PyObjectBase, GeometryPyCXX, the seven `*PyImp.cpp` binding
TUs, PyCXX; stubbed in `ImageStubs.cpp`: console to stderr, the
internal unit scheme only, a trace counter.

Transport: `fcx.host_call` / `fcx.host_fetch`, a two-call
size-then-fetch under wasmtime linker module `fcx`; image side
`_fcx.op(name, id, ...)` with a `HostHandle` proxy; host side
`HandleTable` and `dispatchHostOp`, permission checks before the object
is touched.

Packaging (`c105d058e6`): `tools/pack_image.py` strips the 34 MB build
(28 MB of it DWARF and names) to 10.9 MB (3.1 MB gzip) plus a 16-file
320 KB stdlib slice, and emits the desktop layout `<datadir>/Fcx/
{fcx_image.wasm, Lib/}` from the same list; `install(...)` rules
exist.  Resolution (`WasmtimeRuntime::resolve`): `configure()`, then
preferences `ImagePath`/`StdlibPath`, then `FCX_IMAGE`/`FCX_STDLIB`,
then `<datadir>/Fcx`.  The compiled-module cache lives in `<user
cache>/ExpressionSandbox/<name>-<FNV-1a of the image path>[-budget]
.cwasm` (the image directory is read-only after an install); a
precompiled module instantiates in about 19 ms against about 600 ms of
JIT.  `cMake/FindWasmtime.cmake` greps `wasmtime/config.h` for
`WASMTIME_CONFIG_PROP(void, wasm_exceptions`; `libwasmtime.so` has no
SONAME, so the imported target sets `IMPORTED_NO_SONAME`, which CMake
honours only on a target it knows is SHARED; `FREECAD_BUNDLE_WASMTIME`
defaults ON when the library is outside the prefix and the system
directories.

Two traps from the build: two `nlohmann::json` copies (3.11.2 vendored,
3.12.0 conda) collide in exported symbols with `json` in the signature;
and the v1 catalog ALLOWs addon principals, so a test that expects a
refusal must use a document principal.

### 4.3 The browser tier **[built 2026-08-31, on the WASI image]**

The stripped image runs in the page under pack-first evaluation with
the bridge deliberately NOT attached (a synchronous import cannot await
a Promise); a formula being typed is previewed without a host round
trip.  Needs the exnref exception proposal: Firefox 131+ and Chrome
137+ ship it, Chrome 123 needs `--experimental-wasm-exnref`.  Numbers
(Firefox 136, -O0 image, never re-taken on Release): fetch 128 ms,
compile and instantiate 268 ms, init 31 ms, 140 us per evaluation;
page-load-to-preview 1042 ms dominated by the 10.9 MB fetch.  The
browser tier has not moved to pyodide; where its packages would come
from is sec 6.4 territory, designed only.

### 4.4 Rejected: pyodide hosted by node **[measured 2026-09-01]**

Conda does ship an embeddable engine (`nodejs`, `libnode.so`, 71 MB)
and the boundary is fast (host hop 3.0 us against the WASI bridge's
then-quoted 31.6 us -- really ~7 us, sec 8.1), but a node-hosted
pyodide cannot be confined by subtraction:
after `del globalThis.process`, `js.Function('return
import("node:fs")')()` is still a full read-write escape, because
dynamic `import()` is a V8 realm intrinsic; node's permission model
blocks `process.binding`, which pyodide's own loader uses.  Hence the
bare engine of sec 4.1.  One correction worth keeping: node's JS module
loader and emscripten's `WebAssembly.instantiate` wheel linking are
orthogonal, so "no wheels ever" was the WASI image's own limit, never
pyodide's.

## 5. Bootstrap and packages **[built 2026-09-03]**

### 5.1 Layout

    <user app data>/Pyodide/
        current                 one line: the version that boots
        <version>/              pyodide.js pyodide.mjs pyodide.asm.mjs
                                pyodide.asm.wasm python_stdlib.zip
                                pyodide-lock.json package.json
        packages/               the user's wheels under lock-file names
            manifest.json
    <datadir>/Pyodide/wheels/   fcx_image-<ver>-cp314-cp314-pyodide_<abi>_wasm32.whl

`App::ExpressionSandbox::Pyodide` (`ExpressionPyodide.{h,cpp}`) is the
single owner of these facts: `releases()`, `layout()`,
`verifyDirectory()`, `scopePath()`, the lock lookups,
`missingImport()`.  Overrides: preferences `PyodideUserDir`,
`PyodidePackages`; environment `FCX_PYODIDE_USER`,
`FCX_PYODIDE_PACKAGES`.

### 5.2 Resolve order

Runtime directory: explicit `configure()`, preference `PyodideDir`,
`FCX_PYODIDE`, the user-data `current` marker, `<datadir>/Pyodide`.
Wheel: explicit, `PyodideWheel`, `FCX_PYODIDE_WHEEL`, an
`fcx_image-*.whl` beside the runtime, the shipped wheel whose ABI tag
matches (`wheelForAbi`), `<stdlib>/fcx_image.whl`.  A wheel whose tag
does not match the runtime's `abi_version` is refused at boot.

### 5.3 The pinned table

One entry: version `314.0.6`, ABI `2026_0`, CPython 3.14.2; the six
runtime files with sha256 (npm CDN and GitHub core tarball agree byte
for byte); the core tarball `pyodide-core-314.0.6.tar.bz2` with its
hash.  `initialize()` verifies version and every file hash (about 14
MB, tens of milliseconds, once per instance) and REFUSES anything else;
`FCX_PYODIDE_UNPINNED=1` or preference `PyodideUnpinned` turns the
refusal into a warning that repeats on every boot.

### 5.4 Scoping, and the one trap

The reader and the module loader are confined to three canonical roots
-- the runtime directory, the wheel's directory, the package set --
compared component by component after following symlinks,
case-insensitively on Windows, understanding `file:///C:/x`; never one
widened parent.  **Pyodide's shell-mode `resolvePath` is the
IDENTITY**: `packageBaseUrl` never reaches the reader and the loader
asks for a lock-file wheel by its bare file name, so a relative name
resolves against the runtime directory first and the package set
second (`scopePath`'s `fallbacks`), existing entries only.  A dev tree
stages numpy beside the runtime, so a test that boots from the staged
directory passes vacuously; the offer test boots from a copy holding
only the pinned files.

### 5.5 The installer and the offer

`freecad.pyodide` (`src/Ext/freecad/pyodide/__init__.py`), host Python:
`releases()`, `layout()`, `default_version()`,
`install_runtime(version=None, source="github", progress=None,
make_current=True)` with sources `github` (the 6.8 MB core tarball,
verified, six files read out by name), `jsdelivr` (npm, file by file)
or a local tarball/directory; `list_runtimes()`, `remove_runtime()`,
`set_current()`; `install_package(name, source="index", progress=None,
requested_by="session")` resolving `name` and its `depends` against the
active runtime's lock, depth first, fetching from
`https://cdn.jsdelivr.net/pyodide/v<ver>/full/<file>` or a local
mirror, verifying sha256, writing atomically, rewriting
`manifest.json` (`version`, `runtime`, `abi`, `packages`,
`load_order`); `remove_package()`, `list_packages()`, `manifest()`;
exceptions `RuntimeUnsupported`, `DownloadError`, `VerificationError`.
Downloads go through the Addon Manager's `NetworkManager` with a GUI,
`urllib` headless.  Nothing runs at startup.  A name not in the lock
raises "PyPI packages are not supported yet".

Boot loads `fcx_image` then `loadPackage(names)` in manifest order; a
manifest for another ABI is ignored with a warning.  The offer: the
guest's error reply asks the host one op, `pkg.missing {a: name}`,
for a `ModuleNotFoundError` that leaves the guest UNCAUGHT (2026-09-05;
until then a `sys.meta_path` finder asked at every failed import, and
lark's optional `try: import regex` in BIM's generated parser -- `regex`
is in the lock -- recorded an install offer nobody asked for; stats
now count `pkg.missing:<name>`), and from the expression language's own
`import x` statement (Expression.cpp `ImportModules`, guest build only):
nothing in an expression can catch that failure, and the engine rethrows
it as a Base exception before the reply looks, so the question is asked
at the import site while it is still a `ModuleNotFoundError` -- the
finder retirement had silently lost this path; `SandboxPyodide`'s
python-mode `import numpy` test, not rerun that day, found it on
2026-09-05.  Both sites share `FcxImage::missingImportOffer`.  The host answers from the lock's import map
(304 names): installed -> `ImageHost::scheduleReset()` and the next
evaluation boots with the package; else
`Runtime::requestPending(PkgInstall, name)` records a pending request,
audited as a prompt.  The panel's allow button runs
`freecad.pyodide.install_package(name)`, resets the sandbox
(`App::ExpressionSandbox::resetSandbox()`), and recomputes the blocked
objects -- the same re-run path a grant takes.  Not built: in-place
install into a running guest (P2), PyPI sources (PEP 783
`pyemscripten_<abi>` wheels), the pre-run import scan (dropped: an
`import` fails at the guest's import with the same offer).

### 5.6 Bundled wheels **[built 2026-09-04]**

FreeCAD's own workbench code reaches the guest as pure-Python wheels
beside the fcx_image wheel: `<datadir>/Pyodide/wheels/<dist>-<ver>-py3-
none-any.whl` (the dev tree's `<datadir>/Pyodide` is scanned too),
`Layout::bundled`, one per distribution name, sorted.  The boot loads
them by absolute path after `fcx_image` and before the user's package
set, their directories joining the reader's roots; a wheel that fails
to load is reported on the guest's stderr and skipped, as a package is.
Nothing is installed per user and nothing is downloaded: the wheel is
a build product, matched to the FreeCAD that ships it.

The first is `fcx_draft`, Draft's App side (7.6): `DraftVecUtils`,
`DraftGeomUtils`, `WorkingPlane`, and the `draftgeoutils`, `draftutils`,
`draftfunctions`, `draftmake`, `draftobjects` packages UNMODIFIED from
`src/Mod/Draft` (the CMake lists in `src/Mod/Draft/CMakeLists.txt`,
target `DraftSandboxWheel`, so a source edit repacks), minus
`draftutils/params.py` (a facade, 3.2) and the two support modules
that import `FreeCADGui` unconditionally (`todo`,
`init_draft_statusbar`: GUI-side, reached only under `GuiUp`); plus
what they import that the
guest lacks -- the vendored `lazy_loader`, `freecad.deprecation`
(falling back to `warnings.deprecated` where `typing_extensions` is
absent), and the guest shims under `src/App/ExpressionImage/shims/`:
a `PySide` package carrying the names Draft's App side reaches it
through and nothing that draws (`QT_TRANSLATE_NOOP`,
`QCoreApplication.translate` as the identity, `QTimer.singleShot`
running its callable now, `QLocale().decimalPoint()`), and empty
`Draft_rc`/`Arch_rc` resource modules.  The packer,
`src/Tools/bindings/packSandboxWheel.py`, needs python3 only and is
deterministic (sorted entries, fixed timestamps), so an unchanged
source leaves a byte-identical wheel.  WASI's reference image has no
package loader; bundled wheels are the pyodide runtime's.  Cost: the
first evaluation (the boot) measures 1.80-1.82 s with the 336 KB
wheel and 1.77-1.85 s without (three runs each, FreeCADCmd, routing
ON) -- nothing; importing all 105 modules in the guest is a separate
2.9 s gate step and happens only when workbench code asks for them.
Measure with `setRouting(True)` first: `evaluate()` with routing off
never boots the guest, and `available()` does not either.

`fcx_draft` grew on 2026-09-04 (G1d, BIM): `Draft.py` itself and the
two view provider modules it imports unguarded
(`draftviewproviders.view_base`, `view_draftlink`; their GUI reach
sits under `GuiUp`, and BIM derives view provider classes from
`Draft.ViewProviderDraft` at module level).  The second wheel is
`fcx_bim` (target `BimSandboxWheel`, `src/Mod/BIM/CMakeLists.txt`):
the 43 Arch modules a scripted object's Proxy lives in, UNMODIFIED --
their GUI halves stay behind `if FreeCAD.GuiUp` -- without the
workbench init, the commands, the importers, nativeifc, the GUI-only
modules and the tests; plus the generated SQL parser, an `importers`
package shim (`Arch.py` imports `importDAE.triangulate` at module
level; the shim's raises), and BIM's `Presets` as DATA under
`fcx_resources/Mod/BIM/Presets` (the packer's `--add-data` keeps
every file), where the guest's `FreeCAD.getResourceDir()` resolves,
so `ArchIFCSchema`, `ArchProfile` and `ArchComponent` read their
presets at the path they build natively.  What BIM's App side reads
off `FreeCAD` at import or in a hook and the guest module now
carries: `ParamGet` (read only: every `Get*` is a `prefs.read` through
the `freecad.prefs` module facade, `src/Ext/freecad/prefs.py`; a
`Set*` raises), `Qt.translate`/`QT_TRANSLATE_NOOP` (identity),
`getResourceDir`/`getUserAppDataDir`/`getUserMacroDir` (the latter
two a path that does not exist), `addDocumentObserver`/
`removeDocumentObserver` (registered, never fired), and
`ActiveDocument` -- a module property (the module's class is swapped
for one with it) answered by the `active_doc` op: the transaction
owner's `Document` through `get_attr` on the owner's own handle, None
outside a hook.  The corpus is `bimtests.bim_test_objects` (one of
every Arch class made headless: 68 objects, 59 scripted), the gates
`bimTestObjectsReopenRouted` / `bimTestObjectsBuiltRouted` (7.6).

A COMPILED bundled wheel joined them on 2026-09-05 (7.10): `pivy`,
Coin and `pivy.coin` built for the guest by the standalone cross
project `src/App/PyodideHost/pivy/`, shipped through the
`FREECAD_PIVY_WHEEL` cache variable exactly as the fcx_image wheel is
(installed under `Pyodide/wheels/`, mirrored into the build tree).
The layout scan keeps compiled wheels apart (`Layout::bundledCompiled`,
one per ABI and distribution) and the runtime loads those of the
running fcx_image's ABI after the pure-Python wheels.  It costs about
0.2 s of boot.

Three more pure wheels joined on 2026-09-05 for the forms (7.3, Probe
B): `fcx_widgets` (target `WidgetsSandboxWheel`, packed from
`src/App/ExpressionImage/widgets/`: the `comm` shim over the bridge and
the `IPython` stand-in), and two THIRD-PARTY wheels bundled unchanged
-- `ipywidgets` 8.1.9 from PyPI (not in the pyodide lock) and
`traitlets` 5.14.3 from the pyodide distribution -- named by the
`FREECAD_BUNDLED_WHEELS` cache list and mirrored like the pivy wheel.
Nothing downloads at build time: `scripts/sandbox-fetch-wheels.py DIR`
fetches the two by pinned sha256 (docs/DevEnvironment.md).  Their
declared dependencies are not fetched: `comm` and `IPython` are the
shims, `widgetsnbextension`/`jupyterlab_widgets` are browser assets
ipywidgets never imports.  The three cost 0.02 s of boot (1.62 s
against 1.59 s, three runs each, FreeCADCmd) and nothing until a guest
imports them.

Ground truth: a guest has no sockets by structural absence; the shim
defines no `fetch`, `XMLHttpRequest` or `WebSocket`.  The threat that
network adds is exfiltration, server-side request forgery against the
user's LAN, and beaconing, so the gate is by destination.

### 6.1 Shape

Two injected primitives -- a synchronous `XMLHttpRequest` (what
`urllib3`'s emscripten backend and `requests` use) and an asynchronous
`fetch` completing through the pumped task queue -- both forwarded as
one bridge op, `net.http.request`, to a host `NetClient` behind an
interface (leaning libcurl; Qt Network the fallback if the certificate
store check fails on the Windows and macOS conda builds), after
`NetPolicy::check`.  Redirects re-check every hop; the resolved address
is checked, not only the name.

### 6.2 Grammar and built-in denials

Targets are origin patterns `[scheme://]host[:port]`: a hostname
matches itself; `*.example.com` matches subdomains, never the apex; a
bare `*` is refused (the wide grants are the only "everything"); port
omitted means the scheme's default only; scheme omitted means `https`,
`http` written out.  Deny entries use the same grammar and always win.
Always denied, openable only by an exact-address user allow: loopback,
private ranges (10/8, 172.16/12, 192.168/16, fc00::/7), link-local and
the cloud metadata address 169.254.169.254, unspecified and multicast,
and any destination whose RESOLVED address lands there (DNS rebinding).

### 6.3 Defaults and limits

    permission          document    session   addon
    ------------------  ---------   -------   ----------------
    net.http:<origin>   PROMPT (a)  ALLOW     PROMPT, "always"
    net.http.any        DENY        ALLOW     PROMPT
    net.http.local      DENY        PROMPT    PROMPT (b)
    net.ws:<origin>     N4          N4        N4
    net.socket          DENY        N4        N4

    (a) through the panel, never a document-opened dialog; GET/HEAD
        unless widened.  (b) never persisted past the session.

Limits, not permissions: a 64 MiB response cap, timeouts from the
remaining budget, one in-flight synchronous request per guest, per
principal request and byte counters with a passive-indicator threshold;
`Outcome::Terminated` cancels in-flight requests.  Grants live in the
existing `GrantStore` (generic over `Permission`, no schema change);
one audit line per verdict.  Documents never get network by default;
an addon declares its origins in a manifest the Addon Manager can show.

### 6.4 The browser tier

The same policy runs in the page's shim as defense in depth; the
browser's CORS decides what connects; whether the served-document
server should proxy allowed origins is a separate design.

## 7. GUI for sandboxed Python **[designed 2026-09-03; the linter built; FROZEN 2026-09-08]**

**Frozen 2026-09-08 (1.2, the re-aim).**  What this section built
splits two ways.  The WIDGET LAYER -- `src/Gui/Fw/` (H0), the Qt
subset as models, the `PySide` shim and `freecad.widgets` that let
DraftGui run unmodified on the models, `loadUi` both sides, the
panel dialog -- is the BROWSER'S TOOLKIT and stays maintained: the
next step is G7's DOM view and the shim's op call bound natively so
Draft's panels run on the host without pyodide.  The `gui.*` op
dispatcher in `SandboxGui.cpp` is that shim's native backend and stays
alive through it, frozen in REACH (no new guest-only ops).  The
guest-only pieces -- the InitGui runner (`InitGuiInGuest`, off by
default), the Draft and BIM wheels, the session document from the
guest (S1, S2), the selection and the stock dialogs from the guest
(G3d), the status bar and docks from the guest (7.15) -- are frozen in
place with their gates: kept while green, a gate RETIRED rather than
the guest extended when an upstream Draft or BIM sync breaks it;
revisited after G7 lands, when the native shim path shows what it made
redundant.  G4 (7.16) is sized and NOT built.  The sizings below are
kept as the record and as the door back.  **Audited 2026-09-08 (1.6):
the cut line is the FORM PATH (kept: comm, control, dialogs, ui,
selection, the models, the five form gates -- a document program with
GUI access may need it later) against the WORKBENCH PATH (to drop:
commands, workbenches, main window, status bar, docks, timers, the
session document, doCommand, the InitGui runner, pivy); RULED
2026-09-09 "freeze everything": nothing dropped, this header stands;
1.7 evaluates what the workbench path would still take.**


The user's framing: "since our final goal is to run everything Python in
pyodide, eventually we'll need to find a way to expose GUI function ...
my current thought is to have some kind of bridge, or PySide shim, so
that we don't expose PySide to user code directly."  The test named:
port Draft and BIM (Arch is folded into BIM in this fork).

### 7.1 The decision: FreeCAD's API, not Qt's

A shim that speaks Qt's API is one synchronous hop per property read
(Electron removed exactly that as its `remote` module), is a denylist
over the process, has to be re-implemented over DOM for the browser,
and preserves accidents like building Coin nodes to draw a dimension.
The shim speaks FreeCAD's API, generated as facades with a permission
per op (`gui`: DENY document / ALLOW session and addons):

- **U1 Registration** (data): `addCommand`, `addWorkbench`, icon and
  language paths, preference pages.
- **U2 Host services** (one op each): message/question/input dialogs,
  the file dialog (an `fs` grant), cursor, clipboard (grant),
  `defer(ms, callback)`, hints, plus the ops the linter found missing:
  `openUrl` (a grant), a status-bar message, a theme query,
  `preferences.show`, and composed icons `icon.swatch(color, shape)`
  and `icon.overlay(base, badge)`.
- **U3 Forms**: sec 7.3.
- **U4 Selection, view, edit**: `Selection.*` with observers as
  events, camera get/set, pick queries, `setEdit`/`resetEdit`,
  `runCommand`.  `doCommand(src)` is an eval primitive: it runs under
  the CALLER's principal in the caller's guest, never escalates, and is
  gated by `gui.doCommand` -- DENY document (not promptable), ALLOW
  session, PROMPT addon persisted per addon -- with an audit line
  carrying a hash of the source **[decided]**.
- **U5 The scene**: sec 7.2.
- **U6 Tools**: the 3D event stream (Coin events serialized to guest
  callbacks, coalesced per frame) and the snapper.  The snapper's move
  to C++ stands on cost (its per-move work is geometry queries against
  host shapes) but is deferred: no longer a correctness blocker.
- **U7 Compatibility**: a `PySide`-named module in the guest covering
  the data-level subset (translate, icon references, `Qt` enums, the
  static dialog calls, `QTimer.singleShot`, value types), and the
  porting linter.

### 7.2 The scene: pivy in the guest, mirrored **[decided: "can we just port pivy over to wasm in full? I don't see any security problem with that"]**

Coin and `pivy.coin` compile into the guest; Draft builds real Coin
graphs there; the host replicates them into its scene through a
mirror, field changes coalesced per frame, the widget protocol of 7.3
as the wire (a node is a model whose traits are its fields; the model
classes are generated from Coin's field introspection the way pythreejs
generates from three.js).  All 555 pivy uses in Draft and BIM port
unedited, including the ghost trackers' `writeInventor -> SoInput`
strings, and desktop rendering does not change because the host scene
holds the same nodes.  The user's reading on security is right: Coin
inside the guest exposes nothing; the boundary is the mirror reader,
which enforces a node-type allowlist (nothing carrying a file path or
code: `SoFile`, `SoTexture2` by filename, `SoImage`, `SoWWWInline`,
`SoShaderObject`, `SoCallback`, VRML script and inline nodes), quotas
on node counts and inline texture bytes, a structured wire of type
name plus typed fields (never Coin's Inventor parser on untrusted text
on the host), and a check that a selection node's document path belongs
to the principal.  Reads of the HOST scene (`getSceneGraph`, camera
node, pick, bounding-box and search actions, about 30 uses) stay ops;
FreeCAD's own node types created by name (`SoBrepEdgeSet` x9,
`SoFCSelection`, `SoDatumLabel`, `SoSkipBoundingGroup`) get guest
stand-ins.  Cost: the Coin fork has no emscripten support (Boost
headers, OpenGL, X11, with GLX and EGL behind options); native pivy is
34 MB with symbols, so the guest grows by roughly 15 to 20 MB of wasm.
Probe A (sec 11) decides between real Coin and a Coin-shaped model
library; both keep the mirror.

### 7.3 Forms: the Jupyter widget protocol **[decided: "the Jupyter + pythreejs pair is very fitting for us. I'd like to grow on it"]**

U3's wire is the Jupyter widget protocol (comm open, state as key-value
diffs, binary buffers, custom messages; about forty core models
specified attribute by attribute; two model-side implementations,
ipywidgets in Python and xwidgets in C++).  Guest side: ipywidgets
unchanged in the package set plus a comm shim of about a hundred lines
over the bridge (`comm.create_comm` / `get_comm_manager`, as xeus and
JupyterLite's pyodide kernel do; JupyterLite already runs ipywidgets on
pyodide from a Web Worker).  Host side: widget managers we write -- Qt
first -- plus a FreeCAD widget module registered on both sides for
quantity inputs, a selection input (Fusion 360's `SelectionCommandInput`
shape), color buttons, and a tree/table model widget with typed cells
(the QStandardItem panels of BIM).  `.ui` stays the AUTHORING format:
the guest parses it for object names and classes, the host manager
reads the same file for layout (Qt through `uic`, exact); the U7
subset gives models Qt-flavored accessors so the 25 `loadUi` panels
port with little edit.  Only host-registered model names render; a
widget package's own JavaScript is never loaded; an unknown model is a
labeled placeholder.  No webview escape hatch for now (if the browser
tier ever needs one, MCP Apps is the shape).  Caveats: traitlets is
heavier per attribute than a hand-rolled model; the `HTML` and `Output`
widgets are browser-shaped, native managers support a subset.

**PROBE B PASSED 2026-09-05, first complete run.**  The shape it
settled:

- **Guest**: ipywidgets 8.1.9 and traitlets UNMODIFIED as bundled
  wheels (5.6).  The comm shim is a drop-in `comm` package
  (`src/App/ExpressionImage/widgets/comm/`, the API of jupyter/comm
  0.2.3): `Comm.publish_msg` is one op, `gui.comm [msg_type, comm_id,
  target, data, metadata, buffers]` (bytes cross as CBOR binary), and
  `CommManager` registers itself ONCE as a guest proxy
  (`FreeCADGui._register_comm_manager`, hooks `host_msg`/`host_close`/
  `host_open`) so the host can drive it back.  ipywidgets imports
  `IPython` at import; the real one is 12 wheels, 2.5 MB, 400 modules
  and **1.53 s** of guest import for `get_ipython() -> None` and a
  `display`, so an `IPython` stand-in ships in the same wheel (0.001 s);
  its `display(widget)` shows the widget on the host, the Jupyter idiom
  kept.  `FreeCADGui.showWidget(w, title, where)` / `hideWidget(w)` are
  the explicit form (`gui.widget.show`/`hide` by model id; `where` is
  `panel`, the task panel, or `window`).
- **Host**: `freecad.widgets` (`src/Ext/freecad/widgets/`), the manager
  -- one `Model` per comm (state, live views), `comm_open`/`comm_msg`/
  `comm_close`, `update` applied as a diff to the views, `echo_update`
  ignored, `custom` handed to the views; back to the guest as
  `host_msg(comm_id, {method: update|custom, ...})` on the stand-in --
  and `freecad.widgets.qt`, the Qt manager: a `View` per (model,
  rendering) with `applying` set while the guest's state is written so
  Qt's signals do not echo it, `IPY_MODEL_` references resolved for
  boxes/layout/style, twenty core model names covered (sliders, number
  inputs, Text/Textarea, Button, Checkbox/ToggleButton, Dropdown/
  RadioButtons/Select, Label/HTML, the boxes; Layout's px sizes and
  visibility honored, the style models data), an unknown name a
  labeled placeholder.  A shown root is a `TaskPanel` whose OK/Cancel
  reach the guest as `{"event": "accept"|"reject"}` on the root model.
  The four `gui.comm*`/`gui.widget.*` ops live in `SandboxGui.cpp` and
  are the `gui` permission: a document principal's `IntSlider()` is
  refused at its comm open.
- **Measured** (`scripts/sandbox-widgets-probe.py`, Xvfb, RelWithDebInfo):

      guest import traitlets / ipywidgets    0.047 s / 0.13 s (269 modules)
      six widgets + a VBox                   20 models (layouts, styles),
                                             20 comm opens, 0.5 ms each, 9 ms
      shown as a task panel                  25 ms
      Qt slider step -> guest observer ->    0.22 ms per step: 1 proxy call,
        guest label -> Qt label              2 nested comm ops (echo + label)
      button click (custom, no reply)        0.017 ms
      guest write -> Qt slider               0.175 ms net of a 0.015 ms exec
      boot with the three wheels             +0.02 s

  Two facts worth keeping: every ipywidget opens three comms (its
  Layout and Style models come first), so a form is ~3x its visible
  widgets in models; and a guest's own trait write fires its own
  observers exactly as natively, while the host sends nothing back for
  it (the gate checks `sent` unchanged).  Gate `SandboxWidgets` (2
  cases, 9): the form built, every view's Qt state, both directions,
  OK as an event, window/hide, close and `close_all` dropping the host
  models, the document principal refused.

### 7.4 The toolkit transition **[decided 2026-09-06: two backends, Qt and DOM]**

No Python toolkit has both a Qt and an imgui backend (Dear PyGui,
imgui_bundle, pyimgui are imgui only and immediate mode; Toga has the
shape but neither backend; Slint's royalty-free license requires
attribution and is GPLv3 otherwise).  The transition goes through the
WIDGET LAYER -- a toolkit-neutral model per Qt class, Qt's property
names as the state, an op log for layouts, Qt-named signals -- and
that layer serves exactly two backends: **Qt on the desktop, the DOM
in the browser.**  No in-canvas UI on either tier.  The 2026-09-03
ruling here (a Qt-free manager over bgfx, RmlUi preferred over Dear
ImGui for its flexbox) is WITHDRAWN: G3a made the models Qt-shaped
(7.11), which took the flexbox argument away, and the sizing in 7.12
took the rest.  On the desktop an imgui panel costs accessibility and
the native look and buys nothing the Qt task panel does not already
do; in the browser an imgui panel with a hidden DOM input fixes only
typing, and task panels are mostly typed fields (7.12 has the
comparison; `docs/ThinClient.md` ruled the same for the viewer's
chrome, and its SolidJS inspector is the DOM path already working).

The layer has two halves with one vocabulary.  The GUEST half is
`freecad.widgets` (7.11, built): the models a sandboxed panel builds.
The HOST half (7.12, sized) is the same Qt-shaped classes in C++, so
NATIVE task panels port onto it and the Python Qt manager retires.
User ruling, 2026-09-06: "I still want host side abstraction of
widget layer like in the guest.  it's just that they now only need to
serve two backends.  for now just focus on desktop.  later when Dom
backend is in, all host/guest side task panel come into browser for
free."  Ordering: the host layer's core and first native port (H0,
H1) come BEFORE G3b, so G3b's views are written once, in C++.

### 7.5 What the linter found (`scripts/sandbox_gui_lint.py`)

Run with `.conda/freecad/bin/python3` (Draft and BIM use `match`;
system Python is 3.8).  It buckets every PySide/pivy/FreeCADGui use in
Draft and BIM by longest prefix in a table that is the design above as
an allowlist; `--surface` reports the App-side API members a set of
files uses against the `<Sandbox>` annotations; `--list FILE`, `--all`,
`--no-edit`, `--ui`, `--json`, `--self-test` (25-use fixture).  Live
numbers, 467 files (Draft 243, BIM 224; 19 + 49 `.ui`):

    bucket         uses   files   meaning
    -------------  -----  -----   -------------------------------------
    subset         2187    189    U7 covers it, no edit
    U1              335    169
    U2              310     75    includes composed icons, openUrl, status, theme
    U3             1504    125    forms, including the tree/table model widget
    U4              846    192
    U4.doCommand    247     47
    U5              503     29    Coin, mirrored: ports unedited
    U6              243     62    event callbacks, the snapper
    unmapped        107     33    the residue

    files: clean 189; no edit 118 (112 subset-only, 6 mirror-only);
    form or tool work 127 (10 of them also mirrored); with residue 33.
    Files needing an edit 160 of 467; uses needing an edit 1854, of
    which 1504 are forms.

The residue is event filters and synthetic events (19), BIM state hung
on the `Gui` module (11), filesystem calls (9), rich text in ArchReport
(8), host-scene reads (about 20 across handles, actions and cameras),
an MDI-area walk in WorkingPlane (4), Quarter handles (4), this fork's
live-import switches (4): rewrites in the files that own them or
host-scene query ops, no protocol gap.  The work list is led by forms:
`ArchPrecast.py` (108 U3), `ArchReport.py`, `DraftGui.py`,
`ArchComponent.py`, `ArchCoveringGui.py`, `ArchWindow.py`.  The `.ui`
subset the managers must render: 41 widget classes, 16 of them
FreeCAD's own `Gui::` widgets, the `Pref*` family a third of custom
instances.

Draft's App side (`draftobjects`, `draftgeoutils`, `draftfunctions`,
`draftmake`, 97 files) reaches the GUI through exactly three names:
`QT_TRANSLATE_NOOP`, `QTimer.singleShot`, `getViewDirection`.  BIM's
`Arch*.py` files hold object, view provider and panel together; G1
loads them whole with the subset present.

### 7.6 G1 sized: Draft's App side in the guest

`--surface` over that App side: 187 distinct declared members, 4231
reads (TopoShape 41 used / 5 annotated, GeometryCurve 18, BoundBox 17,
TopoShapeEdge 17, Vector 17, DocumentObject 13 / 2, Rotation 13,
Document 12 / 2, Placement 12, ...), 32 `Part` module names (374 uses),
16 `FreeCAD` names (692 uses, `Vector` 332).  Status 2026-09-04: the
`write_prop` op and the write family are BUILT (3.2); the 187-member
surface is ANNOTATED (3.3: 239 members, 50 facades) and proved on
handles from the guest (`partSurfaceOnHandles`: 31 expressions over a
box and its sub-shapes, curves and surfaces -- handle-tier attributes,
value-tier reads, declared calls with handle arguments dereferenced,
booleans between two handles -- each equal to the host's own answer on
both runtimes).  The `Part` module facade is BUILT (3.2: 30 callables,
`OCC_VERSION`, `OCCError`; gate `partModuleFacade`, 14 constructions
and reads equal to native on both runtimes, the exception mapped).
**G1a gate PASSED 2026-09-04** (`draftgeoutilsOnHandles`): the 16
geometry modules of `draftgeoutils` (all but `geo_arrays`, which
makes document objects) pushed into the guest UNMODIFIED through
`exec` and registered on the host from the same source, 116 calls
covering every public function the fixtures can feed, on 14 host-made
shapes bound as handles, each answer normalised (shapes to kind,
measures and counts; values rounded) and equal as text on both
runtimes; 11 of the 116 fail identically on both sides (curve-only
functions fed an edge, the deprecated `sortEdges`, a `NameError` in
Draft's own `get_spline_normal`, `removeSplitter` on a clean box) --
parity, not a sandbox gap.  What the gate forced into the guest is
listed in 3.2 and above (`bool`/`str` ops, type references,
`ShapeList` as list, `GuiUp`/`Console`/`Base`/unit constants).
The traffic it measured, 116 calls: `release` 4977, `get_attr` 2738,
`call` 587, `mod_call` 153, `bool` 50, `read_prop` 29, `str` 18 --
about 74 hops per call, and 58 percent of them were releases of
proxies the guest let go one at a time.  Step 6 of the coding order
made releases ride the next request or the reply (3.2): the same 116
calls now make 3575 hops -- `get_attr` 2738, `call` 587, `mod_call`
153, `bool` 50, `read_prop` 29, `str` 18, `release` 0 -- 31 per call,
~0.2 ms of crossing per call at sec 8.1's hop cost, and the per-eval
pack cost lost its release hop (8.1).  What remains is dominated by
`get_attr` on shapes and curves; a snapshot op would target exactly
those reads, and this is the count to size it from.
**G1b BUILT 2026-09-04** (`draftWheelInGuest`): Draft's App side boots
with the pyodide guest as the bundled `fcx_draft` wheel (5.6) -- no
`exec`, no stubs; every module of the wheel imports in the guest (the
gate found one unguarded view-provider import in `draftmake/
make_polygon.py`, fixed in Draft the way its siblings guard theirs),
the preference reads answer from the host through the
`draftutils.params` facade (3.2) equal to the host's own, and the G1a
calls agree from the wheel.  Still missing: a `FreeCAD` module facade for `ActiveDocument`
(69 uses, all in `draftmake`/`draftfunctions` -- the make_* commands,
not an `execute()`), rung 2, and a local-wheel source in
`install_package`.  Document-level writes (`addObject` 76 uses,
`removeObject`) are NOT declared: they are what Draft's make_* commands
do, not what an `execute()` does, and rung 2 writes self only -- a
decision to revisit with G1c.  NOT missing, checked 2026-09-04: the
guest-native value classes.  The
in-image `FreeCAD` module has carried `Vector`, `Rotation`,
`Placement`, `Matrix`, `BoundBox` and `Units.Quantity`/`Unit` since the
core carve, and since the draftgeoutils gate also what workbench code
reads before doing geometry: `GuiUp` (0), a `Console` whose `Print*`
write to the guest's stderr, `FreeCAD.Base` (the same classes and
exception types, for `from FreeCAD import Base`), and the unit
constants `Units.Radian`, `Units.Metre`, ... from `Quantity::unitInfo()`
as the host's Units module carries them -- none of it crosses, all of
it lives in the
core carve (ImageDispatch.cpp), so the 1.6 k vector operations never
hop.  Gate passed on both runtimes
(`ExpressionImageEvalTest.draftVecUtilsInGuestZeroHops`): DraftVecUtils
pushed into the guest UNMODIFIED through the `exec` op (its
`draftutils`/`freecad.deprecation` imports stubbed -- those pull PySide
and the parameter store, G1b's loader problem), all 23 public functions
evaluated in the guest and on the host from the same source, results
within 1e-12 (wasm libm included), `stats()` showing zero bridge ops
and zero handles.  Plan: G1a the surface
(gate: every public `draftgeoutils` function run in the guest on
handles, results equal to native); G1b the Draft wheel and the loader;
G1c rung 2 for one principal (gate: one Draft Wire's `execute()`
byte-identical); G1d the Draft and BIM test documents recomputed with
routing ON.  Three decisions pending before G1a: annotate as `call` now
(host CPython) with rung 1 later; Draft in the addon guest rather than
a per-document guest; geometry values bound to the wasm value layer
rather than pure Python (proposal: wasm, because byte-identical shapes
is the gate).

**G1c SIZED 2026-09-04: the Proxy dispatch.**  What exists: a scripted
object's `Proxy` is a host Python instance; `FeaturePythonT::execute()`
(`FeaturePython.h:201`) calls `FeaturePythonImp::execute()`, which
calls `Proxy.execute(obj)` through the Python C API, one of the 22
hooks in `FC_PY_FEATURE_PYTHON` (`execute`, `mustExecute`,
`onBeforeChange`, `onChanged`, `onDocumentRestored`, ...); `imp->init`
re-reads which hooks the Proxy defines whenever the `Proxy` property
changes (`FeaturePython.h:363`).  The Proxy is installed by the class
itself (`obj.Proxy = self`, `DraftObject.__init__`) and persisted by
`PropertyPythonObject` as `<Python module=".." class="..">` plus the
`dumps()` JSON; `Restore` imports the module and allocates the class ON
THE HOST (`PropertyPythonObject.cpp:379-390`) -- the sec 13 gap.
Draft's `Wire.execute()` (`draftobjects/wire.py:124`) reads 14
properties, calls `Part.LineSegment().toShape()`, `Part.Wire()`,
`obj.positionBySupport()`, writes `Shape`, `Area`, `Length`,
`Placement`, `Start`, `End`, and relies on `self.props_changed`, a list
`onChanged` fills and `execute` clears -- so the hooks are one
stateful object, not one function.

*Mechanism (proposed):* the Proxy instance lives in the guest (the
addon guest that already boots the `fcx_draft` wheel); the host's
`Proxy` property holds a STAND-IN.

1. Guest: a registry (host-assigned proxy id -> instance).
   `HostHandle.__setattr__('Proxy', inst)` registers `inst` and sends
   `write_prop Proxy` with a new wire value `{"t":"gproxy", "id",
   "mod", "cls", "hooks":[...]}`, `hooks` = the `FC_PY_FEATURE_PYTHON`
   names the class defines plus `dumps`/`loads`.  Two host->guest ops:
   `proxy_call {id, m, a:[owner handle, ...], owner_h, owner_fc}` (the
   hook call; the object crosses as a fresh transaction handle, exactly
   the `execute(self, obj)` signature) and `proxy_new {id, mod, cls,
   a:[handle]}` (construction; with `alloc` only `cls.__new__`, for
   Restore); a dead stand-in's id rides the release queue.
2. Host: an `App::ExpressionSandbox::GuestProxy` type whose
   `tp_getattro` answers exactly the declared hook names with bound
   forwarders -- `FC_PY_GetCallable` finds only those, so a hook the
   class does not define costs nothing (Wire: `execute`, `onChanged`,
   `onDocumentRestored`, `dumps`, `loads`).  Its `__module__` and
   `__class__.__name__` report the GUEST class, so `Save` writes the
   same `<Python module="draftobjects.wire" class="Wire">` a native
   session writes and the file stays readable by an unrouted FreeCAD.
   `Restore` with routing on: if the guest can import the module (one
   cached round trip per module name) build the stand-in, `proxy_new`
   alloc, forward `loads`; else refuse -- fail closed, no native import
   of a document-chosen module name.  `FeaturePythonImp` itself does
   not change: the stand-in raises the guest's exception type and
   message, and `FeaturePythonT::execute` already turns that into a
   `DocumentObjectExecReturn`.
3. RE-ENTRANCY, the real step.  A guest `execute()` writing `obj.Shape`
   runs the host's `onChanged` INSIDE the `write_prop` op, and that
   hook is in the guest too: a nested round trip while the guest is
   suspended in `fcx_host_call`.  Deferring the hook is not an option
   -- Draft's `props_changed_store`/`props_changed_clear` ordering is
   what `execute` relies on -- so the round trip must nest.  Both
   runtimes can: V8's `Locker` is re-entrant and a wasm export may be
   called from a native callback; wasmtime must be entered through the
   CALLER's context inside a host callback (`wasmtime_caller_context`),
   so the runtime keeps a current-context stack; the single
   `pendingReply` slot is safe because every nested op fetches before
   the outer reply is parked.  `ImageHost` needs a transaction stack
   (owner, deferral, security scope), releases flushed at the outermost
   end only, the budget watchdog armed once.
4. Extension methods.  `obj.positionBySupport()` belongs to
   `Part::AttachExtension` and is injected per INSTANCE
   (`ExtensionContainerPyImp.cpp:138-150`), invisible to the type-MRO
   walk of `facadeMemberLookup`; the guest facade class does not have
   it either.  Annotate `AttachExtensionPy.xml`; the host lookup falls
   back over the container's extension Python types; a handle carries
   its extension facade keys (`"ext":[...]`) and the guest composes the
   proxy class from facade plus extension mixins, cached per
   combination.

*Cost, counted on the 3-point open Wire fixture:* about 45 bridge hops
(read_prop ~24, write_prop 7, get_attr ~4, call 3, mod_call 3, bool 2)
at 5-13 us = 0.3-0.5 ms, plus 7 nested `onChanged` dispatches (one per
write) at ~0.1 ms each and their bodies (`Start`/`End`: ~4 hops), plus
the `execute` dispatch itself (~0.1 ms) -- roughly **1.3-1.8 ms of
crossing per Wire `execute()`** against ~0.15 ms native, so about +1.5
s on a 1000-wire recompute; and ~0.1 ms per property write during a
`make_*` (5-8 writes each).  Writes and hooks dominate, not attribute
reads, so the snapshot op does not help here; batching writes cannot
either without breaking the hook order.  G1d measures the real number.

*Steps, each shipping alone:* (a) `GuestProxy` stand-in and the `Save`
side; (b) the guest registry, `proxy_new`/`proxy_call`, the `Proxy`
setattr; (c) nested transactions on both runtimes; (d) extension
facades; (e) the gate; (f) the `Restore` route.  Gate: a Wire made
natively and one whose Proxy lives in the guest, from the same points:
BRep strings equal, `Points`/`Start`/`End`/`Length`/`Area` equal,
`Shape` arrived through `write_prop`, the saved `<Python>` element
equal.

*Decisions this leaves to the user* -- **both RULED 2026-09-04: (1)
"yes", Document-level writes stay undeclared for rung 2; (2) "allow
read only for params anywhere" = the `prefs.read` row of 2.2, ALLOW for
every principal class, and `draftutils.params` rides it (3.2); the
gate runs under a plain document principal with no grant.**
1. Document-level writes.  Recommendation: stay undeclared for rung 2
   (an `execute()` writes self).  Facts: no `draftobjects` `execute()`
   creates or removes objects; in BIM, `ArchStairs.execute` adds and
   removes its `RailingWire` objects and `ArchReference.execute` removes
   -- those two fail with routing ON until declared, G1d's list.
   **REVISED 2026-09-05 (user ruling, "yes agree"):** `doc.write.self`
   means the owner's DOCUMENT -- the same-origin reading its name always
   had (2.2) -- so the write gate is same-document, not owner-only, and
   `Document.addObject`/`removeObject` on the owner's own document are
   declared under it (3.2).  The alternatives listed for the four
   non-owner writers (an "objects I own" relation; a prompting
   `doc.write.other`) existed only to preserve owner-only and fell
   away with it.  `moveObject` (crosses documents) stays undeclared.
2. The principal of wheel code.  Ruling 1.4 says a Proxy runs as the
   document; `draftutils.params` is `app.query` = PROMPT for documents,
   and `Wire.__init__` reads `MakeFaceMode` -- a prompt per Draft object
   is not acceptable.  The G1b gate never met this: `host.eval` without
   an owner has no principal scope.  Options: a read-only preference
   class (`app.prefs`, ALLOW for documents; the facade reads Draft's own
   groups only) or wheel code running as `addon:draft` even when called
   for a document object.  Recommendation: the former; the ruling
   stands.

**G1c BUILT 2026-09-04, steps (a)-(e); (f) the Restore route remains.**
Built as sized (3.2): the guest registry and `proxy_new`/`proxy_call`,
the `GuestProxy` stand-in (a per-class heap type named after the guest
class, so `Save` is untouched and writes `<Python module=
"draftobjects.wire" class="Wire">`), nested transactions on both
runtimes, extension facades (`AttachExtensionPy.xml` annotated;
`"ext"` on handles).  `FeaturePythonImp` did not change: the stand-in
exposes exactly the hooks the guest class defines plus `dumps`/`loads`
(for a class without its own, the guest answers as the host would
natively: `__getstate__`/`__setstate__` when defined, else the instance
`__dict__`).  Gates: `guestProxyHooksNest` (both runtimes) -- a class
pushed by `exec`, constructed in the guest, its `execute()` doubling a
property through `write_prop` while the host's `onChanged` reaches the
guest again INSIDE that op, the log order `Proxy, Width, execute,
Width, after` exactly the native order, `dumps`/`loads` through the
stand-in; `draftWireInGuest` (pyodide) -- `Draft.make_wire` natively
against a `Part::FeaturePython` twin whose `Wire` Proxy was constructed
in the guest from the bundled wheel, the same three points written
from the host: **BRep byte-identical**, `Points`/`Start`/`End`/
`Length`/`Area`/`Closed`/`MakeFace`/Proxy module, class and `dumps()`
equal, the saved `<Python>` element equal, the Shape arrived through
`write_prop`.  Measured on that Wire: 11 proxy calls (construction,
the hooks the host fired for the writes, `execute`, its nested
`onChanged`s, two `dumps`) and 62 bridge hops -- `read_prop` 25,
`call` 15, `write_prop` 8, `get_attr` 6, `mod_call` 6, `bool` 2 --
against the ~45 hops + 7 nested calls sized above: the `call` count is
the `addProperty` calls of `Wire.__init__` (12) that the sizing left
out, the rest lands where estimated.
**(f) the Restore route BUILT 2026-09-04.**  With routing on
(`proxyRestoreRouted()`, the preference alone then; the preference AND
`available()` since 2026-09-16, 3.5), `PropertyPythonObject::
Restore` on a document object sends `proxy_new alloc` -- the guest
imports the saved module name and allocates `cls.__new__(cls)`, no
`__init__`, what `PyType_GenericAlloc` is natively -- and holds the
stand-in, so `loads`, `onDocumentRestored` and every later hook cross
as they did for a Proxy constructed in the guest; the guest failing to
serve the module (import error, no such class, image unavailable)
leaves the object WITHOUT a Proxy and says so on the console -- never a
native import of a document-chosen name.  Off, the native path is
untouched.  Gates: `guestProxyRestoreRoute` (both runtimes) -- a probe
class present on both sides saved from a guest Proxy, reopened routed
= stand-in, the pre-save log back through `loads`, `execute` crossing
again; a second object whose Proxy names a host-only module reopens
routed with NO Proxy and unrouted with the native instance;
`draftWireRestoreInGuest` (pyodide) -- the G1c Wire saved, reopened
routed = stand-in with equal facts, recomputed BRep byte-identical to
the pre-save shape, reopened unrouted = a native `draftobjects.wire.
Wire` with the same shape.  Not routed: a view provider's Proxy (its
container is no document object; G2).
Still open from the sizing: the two decisions above (ruled, see the
list); `obj.Proxy.<attr>` reads from HOST
Python (Draft's `get_type` reads `Proxy.Type`) have no forwarder yet --
a stand-in `__getattr__` over a `proxy_get` op when G1d needs it; a
guest reset orphans live stand-ins (their hooks raise
`ReferenceError`, never a silent new instance).

**G1d SIZED AND OPENED 2026-09-04: the Draft test document.**  The
harness is `scripts/sandbox-reopen-routed.py` (run under the test rig,
docs/Testing.md sec 1): a document reopened NATIVELY, every object
touched, recomputed and snapshotted -- the honest baseline, since a
re-execute drifts on its own (Draft's Fillet does, natively) -- then
reopened with routing ON through the Restore route, touched,
recomputed, and one row per object: how the Proxy came back (guest
stand-in / host instance / none), shape hash equal or the max vertex
delta, recompute state.  With no argument it builds the Draft test
document, `drafttests.draft_test_objects` -- 111 objects, 70 of them
scripted (every Draft App-side class: lines, wires, fillets, arcs,
circles, ellipses, rectangles, polygons, splines, beziers, points,
facebinders, texts, dimensions, labels, ortho/polar/circular/path/
point arrays as objects and as Links, clones, Shape2DView, working
plane proxies, layers).  First run: all 70 Proxies came back as
stand-ins, none imported on the host, 62 shapes byte-identical, 4
not: Fillet (native drift, gone against the honest baseline),
Shape2DView (`import TechDraw` in the guest: no facade), WPProxy
(`p.Placement = obj.Placement` on the plane it made: the write gate
refused a write on a non-owner handle), Polygon; and the four Link
arrays logged `AttributeError: configLinkProperty` from
`DraftLink.onDocumentRestored` (the Link extension had no facade).
Built the same day: a `TechDraw` module facade (`projectEx`,
`project`, `projectToSVG`, `projectToDXF`, `makeGeomHatch`, under
`geom.call`); `write_prop` on a value handle -- a handle that is not
a property container (a shape the guest built, or a property's copy)
takes a write to a DECLARED attribute of its type through the type's
own setter under `geom.call`, the owner-only gate applying to document
objects alone (3.2); `LinkBaseExtensionPy.xml` annotated
(`configLinkProperty`, `getLinkExtProperty`, `getLinkExtPropertyName`,
`getLinkPropertyInfo`, `setLink`), `configLinkProperty` and `setLink`
in the write family.  Gate `draftTestObjectsReopenRouted` (pyodide):
111 objects, 70 stand-ins, 0 host imports, 0 unserved, 0 invalid, one
shape not byte-identical -- Polygon.  CAUSE FOUND 2026-09-04, by
comparing the bits of `cos`/`sin` of `k * 2pi/5` on the host and in
the guest (python-mode `import math` evaluated through `evaluate()`):
seven of the eight values agree to the bit, `cos(3 * 2pi/5)` does not
-- glibc returns the correctly rounded `-0x1.9e3779b97f4a9p-1`, the
guest's wasm libm (emscripten's musl, FreeBSD msun `cos`, under 1 ULP
but not correctly rounded) returns `...4a8p-1`, and the true value sits
0.03 ULP from the rounding midpoint, a hard case.  One vertex
coordinate of the pentagon is thus one ULP off (2.8e-14 at radius
250), and the line directions and plane origin BRepLib derives from
the points carry it.  Not fixable from the wheel: the guest's `math`
is the runtime's CPython against the runtime's libm; a correctly
rounded `cos` (CORE-MATH) would mean rebuilding pyodide's core, and
routing trig to the host would cost a hop per call.  The user ruled
identity is not required but 1e-9 is far too loose, so the gate now
measures the difference in ULPs of the object's largest coordinate
(`math.ulp(scale)`) and allows at most 4 -- the trig's one ULP plus a
product's own rounding; the harness prints the same number.  The
reference Polygon reads 1.0 ULP.  Traffic for the whole
document's routed recompute: 967 proxy calls, ~2500 hops (`read_prop`
1361, `call` 387, `get_attr` 316, `write_prop` 195, `mod_call` 190,
`bool` 44), 702 handles -- about 1.4 ms of crossing per scripted
object at 8.1's hop cost.
Still open for G1d: (1) BIM -- nothing of Arch is in the guest, so a
BIM document reopens routed with every Proxy failing closed (the
`bimtests/fixtures` file, one `ArchSite._Site`, does exactly that);
needs an `fcx_bim` wheel of BIM's App side plus a corpus the TestArch
suite does not leave behind (its tests delete their objects), then
the list already known: `ArchStairs`/`ArchReference` document-level
writes, 193 `Proxy.<attr>` host reads; (2) the Polygon ULP question
-- RESOLVED above (last-bit libm drift, gated at 4 ULP of the
coordinate); (3) the stand-in `__getattr__` for host reads of
`Proxy.Type` -- BUILT below; (4) the construction dispatch -- SIZED
AND BUILT below.

**G1d, the construction dispatch, SIZED 2026-09-04.**  The fact: a
Draft object MADE in a routed session got a host Proxy, because
`make_polygon` runs on the host and `Polygon(obj)` there constructs
the host class; only reopened documents routed.  Two designs were
sized.  (B) The make layer in the guest -- the host's `Draft` module a
facade, `make_*` running in the guest under the CALLER's principal:
needs document-level writes for that principal (`addObject`, ruled
undeclared for document principals; a user principal is a different
ruling), `App.ActiveDocument` in the guest, handle-returning
`addObject`, and the GuiUp tail of every `make_*` (view provider,
`format_object`, `select`) split off to run on the host after the
guest returns -- a rewrite of the make layer that belongs with G2's
view providers, not before it.  (A) The class boundary -- every
scripted object class's `__new__` asks the host whether the session
routes, and if so constructs the class IN THE GUEST through
`proxy_new` (`cls(*args, **kwargs)` there; its `obj.Proxy = self`
installs the stand-in) and returns the stand-in, which is not an
instance of the class, so Python skips the host `__init__`; the whole
`__init__` runs in the guest, `make_*` is untouched and still calls
`Polygon(obj)`.  The Draft App side has FIVE root classes
(`DraftObject`, `DraftAnnotation`, `Layer`, `LayerContainer`,
`WorkingPlaneProxy`); BIM adds `ArchIFC.IfcRoot` and a few plain
classes when its wheel comes.  A generic hook without touching the
roots does not exist: migrating the host instance at `obj.Proxy =
self` (allocate in the guest, `loads(dumps())`) loses every attribute
`__init__` sets AFTER that line, which is all of them (`self.Type =
tp` follows it in `DraftObject`).  Cost per construction: one
`proxy_new` plus the `__init__`'s own hops (addProperty writes), the
same traffic G1c measured for a Wire.  (A) is built; (B) remains the
route to "one switch" and supersedes (A) when the make layer moves.

**BUILT the same day** (commit after `7b9b4bf224`):
`FreeCAD.ExpressionSandbox.proxyConstruct(cls, *args, **kwargs)`
(`constructGuestProxy` in `ExpressionGuestProxy.cpp`): None when the
session does not route (the Evaluate preference alone, like the
Restore route) or the call is a bare `cls.__new__(cls)` (copy, pickle,
a native alloc) -- the class allocates natively; else the stand-in
(a document object first argument is the owner, `Array(None)` has
none: Draft's link arrays are installed by `addObject(..., attach=
True)`, whose `attach` call reaches the guest as a method read off
the stand-in; the guest registers the instance whether or not
`__init__` installed it), or an error when the guest cannot serve the
module or the class raised -- fail closed, as Restore does, never a
native retry (running `__init__` a second time would repeat its side
effects).  An extension added during `__init__` (WorkingPlaneProxy
adds `Part::AttachExtensionPython` and calls `changeAttacherType` in
the same breath) recomposes the guest proxy's class: `addExtension`
is followed by an `ext` op that re-reads the object's extension
facades.  `addExtension` and `changeAttacherType` joined the write
family (neither was declared before; nothing in a recompute reaches
them, construction does).
`draftobjects.base.new_proxy(cls, *args, **kwargs)` wraps it for the
five roots' `__new__`; the same source runs in the guest, where
`FreeCAD` has no `ExpressionSandbox` and the class allocates natively.
Host reads of Proxy attributes ride the stand-in's `getattr`/`setattr`
(3.2: `proxy_get`/`proxy_set`, methods as forwarders, no trip for a
dunder or an unlisted hook name); Draft reads `Proxy.Type` in 7 App-
side sites and calls `Proxy.getMovableChildren`/`.transform`/`.execute`
in a handful; Draft writes none, BIM writes 22 (`ifcfile`,
`svgcache`, ...).  Gates: `guestProxyAttrsAndConstruct` (both
runtimes: construction from a host class's `__new__`, reads by value,
class attribute, AttributeError, a method forwarder with the object
as owner, a write, a refused handle store, then a recompute through
the guest Proxy; 9 proxy calls), and `draftTestObjectsBuiltRouted`
(pyodide): the Draft test document BUILT with routing on -- every
`make_*` on the host, every Proxy constructed in the guest -- against
the native build, the same counts and the ULP bound as the reopen
gate.

**G1d, BIM, BUILT 2026-09-04 (commit after `72fb1159bc`).**  The
`fcx_bim` wheel (5.6) and the corpus `bimtests.bim_test_objects` (one
of every Arch class made headless: 68 objects, 59 scripted, all valid
natively; Reference alone skipped, it needs an external file), with
the same two gates as Draft's, parameterised (`CorpusGate` in the
test file): `bimTestObjectsReopenRouted` and `bimTestObjectsBuiltRouted`,
REPORTING rather than strict while the list below stands.  What the
gates found, in the order the errors surfaced, each fixed the same
day: `Draft.py` and the two view provider modules it imports were not
in the Draft wheel; `FreeCAD.Qt`, `ParamGet` (with `Get*s`),
`getResourceDir` (+ the presets as data under `fcx_resources`),
`addDocumentObserver`, `ActiveDocument` and the `Units.Length` unit
types missing from the guest module (5.6); undeclared members BIM's
`execute()` paths reach -- `State`, `InListRecursive`,
`OutListRecursive`, `Document.Restoring/Recomputing/Transacting/
getProgramVersion`, `getPropertyByName`, `getPropertyStatus`,
`getDocumentationOfProperty`, `getEnumerationsOfProperty`, TopoShape
`extrude/revolve/makeWires/reversed/isSame/isCoplanar/tessellate/
cleaned/isInside/importBrepFromString/dumps/writeInventor`,
`touch/purgeTouched/renameProperty/setExpression` (write family),
`isAttachedToDocument/getParent`, Sheet `get/getUsedRange/row/column/
getColumnWidth`, `Part.getSortedClusters/makeBox`, `Part.Precision`
(OCCT's compile-time tolerances, a guest class); nine plain Proxy
classes without a root (`_ArchMaterial`, `_ArchMaterialContainer`,
`_ArchMultiMaterial`, `_Axis`, `_AxisSystem`, `ArchGrid`,
`_SectionPlane`, `_ArchSchedule`, `_ArchReport`) given the `__new__`
hook beside `ArchIFC.IfcRoot`'s; and ONE real divergence: ArchFrame's
`profile.Placement.Rotation = rot` -- native FreeCAD writes a value an
attribute handed out back into its parent on a nested write
(`PyObjectBase::setAttributeOf` + `startNotify`), the guest's value
was detached, so the frame's profiles never turned (a 2000 mm
difference).  Now every value a handle proxy hands out is stamped as
that attribute of the proxy (`PyObjectBase::trackAttributeOf`, public
and static, from the prelude's `_fcx.track`), and the write-back is
the proxy's `__setattr__` = `write_prop`, on a value handle through
the type's own setter (3.2).  Result: reopen routed 59/59 Proxies in
the guest, invalid only Stairs, Schedule, Report; built routed 59/59,
plus the pipe Connector -- all four are writes to objects other than
the owner, at the time undeclared (13).  Traffic for the reopen: ~1100
proxy calls, ~9000 hops for 59 objects.  **2026-09-05: the four are
same-document writes, allowed by the revised decision 1 above; see
"G1d CLOSED" at the end of this section for the gates after it.**

**G1d CLOSED 2026-09-05: the BIM corpus gates are STRICT.**  Three
changes: (1) the write gate is same-document (3.2) and
`Document.addObject`/`removeObject` plus the Sheet's cell writes are
declared -- Stairs, PipeConnector and Report recompute routed at once;
(2) TechDraw's `findShapeOutline`/`makeGeomHatch` take
`allowCrazyEdge=True` as a keyword (a scoped `DrawUtil::
CrazyEdgeAllowance` the projector's `isCrazy` consults), and Arch's
area/perimeter computation, ArchTessellation and Draft's Hatch pass it
instead of writing the `Mod/TechDraw/debug` preference around the
call -- natively too, so a recompute never writes the parameter store
(`project`/`projectEx` never consult `isCrazy`; no keyword there);
(3) Schedule's last failure was not a write at all: its execute ends
in `save_ifc_props` -> `nativeifc.ifc_psets.edit_pset`, which natively
looks the object's IFC file up and returns when there is none, and
nothing of `nativeifc` (ifcopenshell) is in the guest -- the fcx_bim
wheel now carries a `nativeifc` shim (shims/README.md) that answers
that path the native way for a plain document and raises
`IfcUnavailableError` for an IFC-backed object.  Found on the way:
a guest exception reached the host as its last line only, so the
reply now carries the formatted traceback (`tb`, ImageResult::
traceback) and a failed hook raises message + traceback, as native
FreeCAD prints for a failed execute(); and `pkg.missing` is counted
per module name in the stats.  Gates: reopen routed 68 objects, 59
guest Proxies, 0 host, 0 invalid, differ Pipe001 at 0.25 ULP of its
largest coordinate (4.5e-13); built routed the same.  Traffic for the
reopen: 1218 proxy calls, ~11000 hops.
Found by recomputing the routed corpus a SECOND time, and CLOSED
2026-09-05: ArchReport caches its Result sheet on the Proxy
(`self.spreadsheet = o`) and the next execute reads it -- a HANDLE from
the previous transaction, gone with it -> `ReferenceError: stale host
handle` (ArchSchedule's same cache sits under a bare `except` and
silently returned None instead).  Caching a document object on a Proxy
across hooks is a native pattern; the fix is durable document-object
handles (3.2): a `[document, name]` key on every DocumentObject and
Document handle, re-resolved by the guest on a stale id through one
`resolve` op (owner's document only), `__eq__`/`__hash__` by key, and
a stale handle used as an argument re-resolved while decoding.  The
corpus gates now recompute twice (the harness too) and stay green:
Draft and BIM, reopened and built.

### 7.7 Decisions, numbered

1. `.ui` as the form language -- amended: the authoring format; the
   runtime model is the widget tree (7.3).
2. The snapper to C++ -- stands on cost, deferred.
3. Dimension view provider to C++ -- WITHDRAWN (7.2).
4. `doCommand` permission-controlled, never escalates -- stands.
5. G1 before G2 -- stands.
6. pivy in the guest, the scene mirrored (7.2).
7. The Jupyter widget protocol is U3's wire, ipywidgets the guest
   library (7.3).
8. The toolkit transition goes through the widget layer, two
   backends: Qt on the desktop, the DOM in the browser (7.4).
9. The host half of the widget layer is built, in C++, and native
   task panels port onto it; no imgui on either tier (7.12).

### 7.8 Prior art

    design                      pattern                                   backs
    --------------------------  ----------------------------------------  -----------
    VS Code extension host      contribution points, thin service API,    U1, U2, U4,
                                TreeDataProvider, webview escape hatch,   tree model
                                remote extension host
    Zed extensions              "describes what to render as data, Zed    U3
                                renders natively"; events return patches;
                                GPU access rejected
    Figma plugins               QuickJS in wasm after the Realms shim      runtime,
                                proved unsafe; typed document API; UI     network
                                iframe; manifest network allowlist
    Jupyter widgets, JupyterLite kernel models, front-end views, state   U3 wire,
                                diffs over comm; ipywidgets on pyodide    browser tier
    pythreejs, xthreejs         a scene graph as widget models, classes   the mirror
                                generated from a per-class config
    xwidgets, euporie           C++ model side; a non-DOM renderer        host managers
    Cash App Redwood, Zipline   logic in a QuickJS guest, schema-generated U3 wire,
                                widget protocol, native renderers         versioning
    Office.js                   proxy objects queue, one sync flushes     batching
    Fusion 360 command inputs   typed inputs, host renders, events        widget module
    Onshape FeatureScript       annotated parameters, generated dialog    same
    Adaptive Cards, Lyft Canvas declarative schema per host; versioning;  schema,
                                unknown-component fallback                fallback
    Unity UI Toolkit            retained UXML, one renderer everywhere    .ui authoring
    MCP Apps                    sandboxed iframe + JSON-RPC, no backchannel escape hatch
    Blender layout API          neutral vocabulary but draw() per redraw  why retained
                                in process with full access

### 7.9 G2 sized: Draft and BIM register from the guest **[sized 2026-09-05]**

G2 is U1 + U2 + U7 (7.1): the workbenches' `InitGui.py` runs in the
guest, their commands and workbenches exist on the host as stand-ins,
and the host services they reach at registration are ops.  The host
facts that size it (`src/Gui`, 2026-09-05):

- **A Python command** is `Gui::PythonCommand` (or `PythonGroupCommand`
  when the object has `GetCommands`) holding the object.  `GetResources()`
  is read ONCE at construction and again on a language change (a dict
  of strings and bools: MenuText, ToolTip, StatusTip, Pixmap, Accel,
  WhatsThis, CmdType, Checkable, Exclusive, DropDownMenu); `IsActive()`
  is polled on every UI update tick and must be a real bool (anything
  else, or an exception, reads as inactive); `Activated()` (or
  `Activated(index)` for a checkable one) on trigger; `GetCommands()`
  names, `GetDefaultCommand()` an index, `OnActionInit()` once,
  `CmdHelpURL()` on help.  Only `IsActive` is hot.  The command's group
  name is derived from the CALLER's file path (`Mod/<Group>/...`), and
  the optional "activation string" makes `Activated` a host-side command
  line that never calls Python.
- **A workbench handler** crosses strings only: `GetClassName()`,
  `Initialize()`, `Activated()`, `Deactivated()`, `ContextMenu(recipient)`;
  `MenuText`/`ToolTip`/`Icon` are read at registration; the host WRITES
  `__Workbench__` (the C++ workbench's own Python object) onto the
  handler after `GetClassName`, and the handler's `appendToolbar`,
  `appendMenu`, `appendContextMenu`, `appendCommandbar`, the removes and
  the lists all go through it.  `addWorkbench` accepts an instance or a
  subclass of `__main__.Workbench`, instantiates a class itself, and keys
  the registry by the CLASS NAME.
- **`InitGui.py`** is not imported: `FreeCADGuiInit.RunInitGuiPy` execs
  the file's text in its own scope, with `FreeCAD`, `App`, `Gui`,
  `FreeCADGui`, `Log`, `Err`, `Msg`, `Workbench` as globals, and a
  failure is logged, never fatal.
- **Resources.** `addIconPath(":/icons")` works because `import Draft_rc`
  just registered the compiled Qt resources in the HOST's resource
  system; a guest cannot register a qrc.  `addPreferencePage` has a
  `.ui`-path form (data: the host's `PrefPageUiProducer`) and a class
  form (the host instantiates a Python class -- U3, G3).
- **View providers** are NOT G2.  `ViewProviderFeaturePythonImp` binds 46
  hooks; `getIcon`, `getExtraIcons`, `setupContextMenu`, `iconMouseEvent`
  take or return Qt objects, `getElement`, `getElementPicked`,
  `getDetail`, `getDetailPath` take or return Coin objects, and `attach`
  is where a proxy builds Coin nodes (`vobj.addDisplayMode(node, mode)`).
  That is the mirror (G4, after Probe A); a view provider's Proxy stays
  native until then (13).
- **No C++ test constructs a `Gui::Application`**; the Gui-side gates are
  Python test modules run under the Gui binary (`FreeCAD -t <Module>`,
  the way `SandboxPyodide` already runs there), on Xvfb with the rig of
  docs/Testing.md (`env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb
  xvfb-run`; `offscreen` crashes in `QOpenGLWidget`).

**What Draft's `Initialize()` needs before it appends one toolbar:**
`import DraftTools`, which imports `DraftGui` (a `DraftToolBar()` of Qt
widgets is BUILT at import), `gui_snapper` (a `Snapper()` at import) and
`gui_trackers` (pivy at import) -- U3, U5 and U6 in the first three
lines.  BIM's `createTools` does the same through `DraftTools` and
`bimcommands`.  So G2 cannot gate on Draft's toolbars appearing: the
registration MECHANISM is G2a with its own gate, and G2b runs the real
`InitGui.py`s in the guest to measure how far they get -- that residue
is G3's list, not a G2 failure.

**G2a, the mechanism (build order):**

1. A guest `FreeCADGui` module in the prelude beside `FreeCAD`:
   `Workbench` base class (its `append*`/`remove*`/`list*` are one op
   `gui.wb {name, m, a}` on the workbench's REGISTERED name -- the guest
   never holds `__Workbench__`), `addCommand(name, obj, activation=None)`
   (the object registers as a guest proxy with the COMMAND hook list and
   crosses as its descriptor: `gui.cmd.add {name, desc, group,
   activation}`), `addWorkbench(cls | inst)` (instantiated in the guest,
   registered with the WORKBENCH hook list, MenuText/ToolTip/Icon read
   there: `gui.wb.add {name, desc, MenuText, ToolTip, Icon}`),
   `addIconPath`, `addLanguagePath`, `addPreferencePage(ui, group)` (the
   `.ui` form; a class is a TypeError until G3), `listCommands`,
   `listWorkbenches`, `getWorkbench` (the guest instance), `activeWorkbench`,
   `updateLocale` (no-op).  `FreeCAD.GuiUp` stays 0: the App side's
   `if App.GuiUp:` imports keep their meaning, there is no GUI in the
   guest.
2. A bridge op REGISTRY on the host (`registerBridgeOps(prefix,
   handler)` in `ExpressionImageBridge.h`, consulted by `dispatchHostOp`
   before "unknown bridge op"), so `Gui` owns every `gui.*` op without
   `App` linking `Gui`; `src/Gui/SandboxGui.cpp` registers them when
   `Gui::Application` is constructed.  Every `gui.*` op is
   `checkPermission(Permission::Gui)`: the catalog's DENY for a document
   (not promptable), ALLOW for session and addons.
3. The command object on the host IS the stand-in (`makeGuestProxy` of
   the descriptor): `new PythonCommand(name, standin)` or
   `PythonGroupCommand` when the descriptor lists `GetCommands`, the
   group name from the op.  The host's stand-in answers a missing
   command or workbench hook without a trip (`isHookName` learns the GUI
   names; the guest's `_proxy_register` takes the hook list), so the
   `hasattr("IsActive")` of every poll costs nothing.
4. The workbench on the host is a WRAPPER, `class GuestWorkbench(
   Workbench)` holding the stand-in and forwarding the five hooks: the
   host writes `__Workbench__` onto the handler, and a stand-in refuses
   host objects (3.2), so the wrapper takes it; `gui.wb` calls the
   wrapper's base-class method `m` (an allowlist: the fourteen
   `Workbench` methods) on the registered name.
5. Principal: registration and every forwarded hook run as `session`
   (the proxy call has no owner) -- the built-in workbenches.  An addon's
   principal (`addon:<name>`) is G2b's: the InitGui runner pushes the
   scope for the exec, and the stand-in then has to carry it for the
   hooks the host calls later.
6. `FreeCAD.ExpressionSandbox.exec(source, module)` on the host (the
   `exec` op the gtests already use), so a Python test can push a probe
   module.

**Gate G2a** (`SandboxGui`, a Test-workbench module under the Gui
binary): a probe module exec'd in the guest registers a workbench, a
plain command, a checkable command and a group; the host sees them in
`listCommands()`/`listWorkbenches()`; `Command.get(name).getInfo()`
equals the guest's `GetResources`; `IsActive` follows a guest flag;
`runCommand` crosses `Activated` (a guest counter); `activateWorkbench`
crosses `Initialize`, and the guest's `appendToolbar`/`appendMenu`
produce host toolbars (`listToolbars`, `getToolbarItems`); switching
away crosses `Deactivated`; a DOCUMENT principal calling
`FreeCADGui.addIconPath` is refused with `PermissionError`; with
routing off nothing of the above exists.

**G2a BUILT 2026-09-05** as sized, with three facts the build added:
the registry's request and reply cross App/Gui as CBOR bytes (a json
in a signature does not link between the two nlohmann copies, 12); the
registry is consulted BEFORE the handle ops resolve `h` (a `gui.*` op
carries no handle, and the dispatcher's stale-handle check came first);
and the host macro is scoped to `SandboxGui.cpp` alone
(`set_source_files_properties`), since a Gui-wide define means a full
Gui rebuild.  Gate `SandboxGui` (2 cases) green on the first complete
run: three commands and the workbench registered from the guest, the
resource dict read through the stand-in, `IsActive` following a guest
flag, `Activated` crossing for the plain and the checkable command,
`activateWorkbench` crossing `Initialize` (the toolbar and menu appended
from the guest exist on the host, and the guest's own `listToolbars`/
`getToolbarItems` read them back equal), `Activated` seeing itself as
the active workbench, `Deactivated` on switching back, a second
registration under the same names replacing the first, and a document
principal refused (`Permission denied: gui`).  A document-principal
`evaluate` that touched `FreeCADGui.activeWorkbench()` was refused too
-- the test had to read that fact from the guest's `Activated` hook,
which runs as session.  Suites after: pyodide 97/97, wasi 86 + 11
skipped.

**G2b MEASURED 2026-09-05, before the runner exists:** the real
`InitGui.py` of Draft and of BIM, exec'd unmodified in the guest with
the runner's globals (`FreeCAD`, `App`, `Gui`, `FreeCADGui`, `Workbench`,
`Log`, `Err`, `Msg`, an empty `FreeCAD.__unit_test__`), the native
workbench removed first:

    module level   Draft: OK -- 1 gui.wb.add, 4 gui.pref_page (the .ui
                   resources exist on the host because the native
                   Draft_rc had been imported at startup: the runner
                   needs gui.rc).  BIM: OK -- 1 gui.wb.add, 5
                   gui.pref_page, 4 mod_call (prefs).
    activation     Draft: the host activates it; Initialize() runs in
                   the guest and RETURNS AT ITS FIRST LINE -- `from pivy
                   import coin` fails, "Pivy not found, Draft Workbench
                   will be disabled" -- so no icon path, no language
                   path, no toolbar; Activated() and Deactivated() cross
                   and run (the WorkingPlane/grid_observer view
                   observers are GUI-only, warned about).  BIM:
                   Initialize() crosses gui.icon_path and gui.lang_path,
                   then `import DraftTools` -- not in any wheel --
                   ModuleNotFoundError.

So the first thing either `Initialize()` needs is not a form: it is
pivy (Draft's self-test) and `DraftTools` (which imports `DraftGui`'s
widgets and `gui_snapper` at module level).  That is Probe A and G3
in that order; the runner itself is a small switch and waits for
something to switch to.  A host `activateWorkbench` whose `Initialize`
raises ends in a modal "Workbench failure" box (`Application.cpp`
~1850) -- under Xvfb that is a hang, so a probe calls the guest's
`Initialize` through `exec` instead.

**G2b, the runner:** a module whose GUI side is bundled as a
wheel runs its `InitGui.py` in the guest instead of natively (the
`Evaluate` preference plus the wheel's presence; native otherwise), with
`gui.rc {module}` (the host imports `<Mod root>/<name>_rc.py`, compiled
Qt resources, the one host import G2 makes, data not code) and the
exec's principal.  A guest reset (a package install) kills every
registered stand-in (`ReferenceError` on the next hook), so the runner
re-runs the guest InitGui's after a reset, replacing the commands.
With the runner on, FreeCAD's start boots the guest (about 1.8 s).
Cost of the mechanism: one hop per `IsActive` poll per guest command
(some 60 visible commands times 5 us per tick), one hop per activation
plus whatever the command does -- U4 ops, G3 and later.

**G2b BUILT 2026-09-06**, as sized with four departures:

- **The switch is its own preference**, `Expression/Sandbox:
  InitGuiInGuest` (default OFF; the rig's `FCX_INITGUI_IN_GUEST=1|0`
  overrides it), not `Evaluate`: with the residue below, `Evaluate`
  alone would have taken Draft's status bar and BIM's tool bars away
  from everyone routing expressions.  It folds into `Evaluate` when
  the residue is gone.  The decision (`GuestInitGuiWanted` in
  `FreeCADGuiInit.py`): the switch, a build with the host, and a
  bundled wheel named `fcx_<module>` (`pyodideLayout()['bundled']`,
  the wheels by distribution name).  `RunInitGuiPy` defers to
  `RunInitGuiInGuest` for such a module; every other module's
  InitGui.py runs natively as before.
- **No `gui.rc` op.**  The runner imports the module's `<Mod>/*_rc.py`
  on the host itself before the exec (Draft's is `Draft_rc`, BIM's is
  `Arch_rc`: the name is the module's to know, not the guest's), and
  the guest's `Draft_rc`/`Arch_rc` shims stay empty.
- **The guest half is in the fcx_widgets wheel**, not the prelude:
  `FreeCADGui._run_initgui(source, path, module, tops)` execs the
  file's text with the native runner's globals (`FreeCAD`, `App`,
  `Gui`, `FreeCADGui`, `Workbench`, `Log`, `Err`, `Msg`) and derives a
  command's GROUP from the wheel its class came from (`tops`, the
  wheel's top-level names, read from the zip on the host): the
  host's own rule is the caller's `Mod/<Group>/` path, and a wheel
  module has no such path.
- **A guest hook's failure is an error in the report view**, not the
  modal "Workbench failure" box a native handler's raise ends in: the
  wrapper's `Initialize`/`Activated`/`Deactivated` catch and print the
  guest's traceback, and the workbench comes up with what it
  registered before the raise -- the InitGui.py failure rule ("logged,
  never fatal") extended to the hooks, because a guest workbench is
  partial by design until G3d and G4, and a modal at startup under
  Xvfb is a hang.

**The reset, built:** `ImageHost::bootCount()` counts guests;
`addBootListener` fires after the outermost call that booted one
returns (never inside it), and `SandboxGui.cpp` queues
`FreeCADGui._onGuestBoot(n)` on the event loop, which re-runs every
InitGui.py stamped with an earlier boot and re-activates the active
workbench if it was a guest one.  Two host bugs surfaced on the way,
both fixed: a stand-in is now stamped with its guest (`(boot, id)` is
the identity of a guest proxy: a fresh guest numbers from 1 again, and
the old guest's stand-in of the same number was answering for -- and
on its death dropping -- the new guest's proxy; a stale stand-in now
raises `ReferenceError: guest proxy N belongs to a sandbox guest that
was reset`, and `teardown()` discards the drops queued for the dead
guest), and `Application::initializeWorkbench`'s once-only guard kept
a removed workbench's NAME, so a handler registered again under it
never ran `Initialize()` -- it runs now whenever no C++ workbench of
that name exists yet.  After a reset one UI tick of `IsActive` polls
reaches the stale stand-ins (two `ReferenceError` lines in the report
view) before the re-run replaces them.

**The measurement -- how far the two `Initialize()`s get now**, with
the wheels grown for it (Draft: `DraftTools`, every `draftguitools`
module, the task panels, `init_draft_statusbar`; BIM: `bimcommands`,
`BimStatus`, a `PartGui` shim, `nativeifc.ifc_commands` answering an
empty tool bar and `ifc_observer` no-ops -- no ifcopenshell in the
guest) and the small gaps filled on the way (`FreeCADGui.UserInput` as
the host's IntEnum mirrored by value on first use, `InputHint` and
`HintManager` crossing as data to the main window's `showHint`,
`getMainWindow().getWindowsOfType()` answering none, `Base.TypeId` as
a name, `draftutils.params._param_observer_start` a declared STUB in
the facade -- the native function is `if App.GuiUp:`, 0 in the guest):

    Draft   InitGui module level in the guest at startup; Initialize()
            on activation: pivy's self-test, DraftTools imported (82
            Draft_* commands registered from the guest, Draft_Hatch
            excepted: its registration sits under `if FreeCAD.GuiUp`),
            DraftGui's tray built, the five tool bars and the menus
            appended and filled on the host.  Activated(): the snapper
            shows, then `init_draft_statusbar` stops at the STATUS BAR
            (`statusBar().findChild`, `addStatusBarItem`: widgets in
            the status bar are not in the widget layer yet); the
            WorkingPlane/grid_observer view observers warn (G4).
            IsActive(): False for every Creator -- `get_3d_view()`
            needs a 3D view (G4); the poll crosses (one proxy call).
    BIM     InitGui module level OK; Initialize(): icon and language
            paths cross, `bimcommands` registers 32 commands and stops
            at `BimCovering` -- the one module of 83 that imports
            FreeCADGui under `if FreeCAD.GuiUp` and registers its
            command outside it -- so createTools never reaches its
            tool bars; Activated()/Deactivated() fail on the missing
            `draftingtools` the same way.

So the wall both stopped at was **the `GuiUp` ruling** (G2a: "FreeCAD
.GuiUp stays 0"), made when the guest had no GUI at all.  With the
GUI side in the guest, two registrations hid behind it (Draft_Hatch,
BimCovering -- the latter taking all of BIM's tool bars with it), and
the App side's `if App.GuiUp:` branches (view providers, Qt imports,
selection) are what the ruling protected for the document guests that
share the instance.  Gate `SandboxInitGui` (4 cases: the decision,
Draft and BIM from the guest, the reset re-run) needs a session whose
startup ran the runner, so it runs in a process of its own with
`FCX_INITGUI_IN_GUEST=1` and skips in the default gate list.

**GuiUp FLIPPED 2026-09-06, user ruling ("flip it").**  The guest's
`FreeCAD.GuiUp` is the host's: `ImageHost` reads the host module's
value after a boot and sends `FreeCAD.GuiUp = 1` as the fresh guest's
first request (a headless host -- FreeCADCmd, the test binary --
leaves the guest's 0, so the `*BuiltRouted` gtests measure what they
always did).  What the flip needed, built the same day:

- **The wheels**: every Draft view provider (the objects' `if
  App.GuiUp:` imports reach all of them), BIM's `BimStatus`; shims for
  `QDesktopServices`, `QFileSystemModel` (a base class BIM's library
  browser derives from at import), `QtCore.Slot`.
- **An object's own view provider is in its write scope.**  Draft's
  Dimension `execute()` calls `obj.ViewObject.update()` under GuiUp;
  BIM's Stairs hides its base's view.  `Gui.ViewProviderPy.xml` and
  `ViewProviderDocumentObjectPy.xml` joined the annotated XMLs
  (`show`, `hide`, `isVisible`, `signalChangeIcon`, `update` as call
  tier, `Object` as a handle), the four are OpCall's "view family"
  under the same-document write gate, and `documentOf` resolves a
  view provider handle (any type whose MRO carries
  `Gui.ViewProviderDocumentObject`) to its Object's document, through
  Python -- App links no Gui type.  Property writes on a view
  provider ride the same gate.
- **`gui.user_input` is data** any principal may read: Draft's tool
  modules read the enum at import, and a document guest imports them
  under GuiUp now.
- **A registration a document's import triggers is deferred**, not
  refused: `import Draft` reaches `gui_hatch`, whose module registers
  `Draft_Hatch`; the prelude's `addCommand` queues what the host
  refuses for the principal and flushes the queue with the next
  registration the session makes (Draft's Initialize).  A registration
  only names wheel code.  Widget comm traffic is NOT deferred (tried
  and reverted the same day: a document's models would reach the host
  with the session's next traffic, and the widget gates' "a document
  principal is refused" is the contract) -- so a document guest cannot
  import `DraftTools` (DraftGui builds its tray at import), which is
  BIM's Rebar below.
- **The main window shim** answers `getActiveWindow()` None,
  `getWindows()` empty, `findChild(QMdiArea)` a stub whose
  `subWindowActivated` never fires (the view observers connect to it),
  `getIcon` as data.
- **Two host bugs the flip exposed, fixed.**  `ActionGroup::actions()`
  cached raw `QAction*` past their deletion (a group's action belongs
  to another command; the re-run after a reset replaced 82 of them
  while Draft's groups held theirs, and `Command::testActive` read a
  dead one -- SIGSEGV); every action the group lists is now tracked to
  its destruction.  And the corpus gate's process hung in `exit()`:
  `pthread_cond_destroy` on `MeshLevelSource.cpp`'s static condition
  variable with its refine threads still waiting -- a renderer bug,
  fixed 2026-09-07 at the source (sec 12): the level workers are
  joinable and `PartGui::shutdownMeshLevelWorkers()` stops and joins
  them on the application's `aboutToQuit`; the rig's `os._exit`
  workaround is gone and the gate's process exits normally.

**Measured with the flip** (gate `SandboxCorpusGui`, the G1 corpus
built under the GUI in a routed session and compared with a native
build, object by object -- the GUI twin of the `*BuiltRouted` gtests):

    Draft   111 objects, 70 guest Proxies, 0 invalid, every shape
            equal (Polygon 0 ULP); the view providers native (the
            corpus makes its objects on the host).  STRICT.
            Layer's onChanged calls its view provider's Proxy method
            (`change_view_properties`): an error line, not a failure.
    BIM     68 objects, 57 guest Proxies.  Invalid: the 3 BuildingParts
            (execute calls `obj.ViewObject.Proxy.onChanged`, a host
            Python object's method -- undeclared, G4).  Proxy stayed
            native/None: Rebar (its module imports `bimcommands` under
            GuiUp, which imports DraftTools, which builds Draft's tray
            -- refused for a document), ArchReport (natively broken in
            this fork's GUI: `ToggleVisibility`; in the guest
            `QSyntaxHighlighter` next).  ReportResult missing for the
            same reason; Pipe001 a quarter ULP.  NOT strict: the list
            above is the record, `guest >= 50` and no native Proxy are
            asserted.
    InitGui Draft as before; BIM's `Initialize()` now completes -- its
            tool bars on the host from the guest; `Activated()` stops
            at `QDockWidget` (BimViews) and, for Draft, the status bar.

Suites after the flip: `SandboxCorpusGui` 2/2, `SandboxInitGui` 4/4,
the six other GUI gate modules 13/13, `Tests_run
--gtest_filter='Expression*'` 104 passed + 1 skipped.

### 7.10 Probe A sized: Coin and pivy in the guest **[sized 2026-09-05]**

What 7.9's measurement made the next blocker: Draft's `Initialize()`
opens with `from pivy import coin`.  The facts (the Coin fork on
`LinkVibe`, `~/works/sw/pivy` on `rt-0.6.10`, the guest toolchain):

- **Coin's hard build dependency is Boost headers only** (`scoped_ptr`,
  `intrusive_ptr`, `lexical_cast` ...).  expat is vendored
  (`src/xml/expat`); zlib, bzip2, freetype, fontconfig, simage, GLU,
  OpenAL are `*_RUNTIME_LINKING=ON` by default, never found at
  configure time and `dlopen`ed through `src/glue/dl.cpp` -- under wasm
  those opens return NULL and each feature turns itself off (fonts:
  Coin's built-in bitmap font remains).  `src/threads` compiles
  unconditionally against emscripten's pthread headers (no `-pthread`:
  pyodide is single-threaded); `COIN_THREADSAFE` is OFF already.
- **No no-GL path exists**: `find_package(OpenGL REQUIRED)` is
  unconditional, `find_package(X11 REQUIRED)` fires because `UNIX` is
  true under emcmake (behind `COIN_BUILD_GLX`, OFF-able, with
  `COIN_BUILD_EGL`).  Zero `EMSCRIPTEN` mentions.  Six files include a
  real GL header; 168 include `Inventor/system/gl.h`, a configure-time
  substitution; extension entry points (263 of them) go through the
  glue and resolve at runtime via `cc_glglue_getprocaddress` (NULL =
  absent, fine); but 73 files under `elements/GL`, `nodes`,
  `shapenodes`, `rendering`, `shaders` call core desktop `gl*()`
  directly.  emscripten's `GL/gl.h` declares them all, so it COMPILES;
  nothing implements them in a side module, so it fails at LOAD.  The
  fix is a generated stub translation unit (every core `gl*` symbol as
  an empty function, `glGetString`/`glGetIntegerv` answering "no GL"),
  which is right for the guest: the guest never renders, the host does
  (7.2, the mirror).
- **Nothing fork-specific in the way**: no Qt, no bgfx in Coin's CMake;
  the fork's additions are the `CoinRT` output name, the `coin_fork_abi`
  C API, elements and shapes.
- **pivy** is one SWIG extension (`interfaces/coin.i` over a 736-line
  header list -> a 17.8 MB `coinPYTHON_wrap.cxx`, a 2.0 MB `coin.py`,
  a 33.8 MB `_coin.so` with symbols natively); SoQt/Qt are looked up
  only `if (SoQt_FOUND)`, so a guest build without SoQt drops
  `pivy.gui` by itself; `fake_headers/` already stubs GL/X11/Qt for
  SWIG's parse.  SWIG 4.4.1 is in `.conda/freecad`.
- **The guest toolchain** (`src/App/PyodideHost/guest`, emsdk 5.0.3,
  pyodide xbuildenv 314.0.6, `-fwasm-exceptions -sSIDE_MODULE=2 -flto`,
  `make_wheel.py` taking directories) has NO compiled third-party
  library yet -- Coin would be the first.  `SIDE_MODULE=2` exports
  only listed symbols, so Coin links STATICALLY into `_coin.so` (one
  module, the native shape) rather than as a second side module.
  Today's guest: 1.4 MB `.so`, 0.38 MB wheel.

**The probe, in order:** (1) the Coin fork configures and builds with
`emcmake` -- `COIN_BUILD_GLX=OFF`, `COIN_BUILD_EGL=OFF`, the OpenGL
`find_package` made conditional (a fork commit on `LinkVibe`: an
`COIN_BUILD_GL_STUB` option that skips the GL/X11 lookups and adds the
stub TU), `-fwasm-exceptions`, no `-pthread`, static library; (2) pivy
against it with the guest's Python headers, SWIG 4.4.1, no SoQt, the
single `_coin.so` as `SIDE_MODULE=2` exporting `PyInit__coin`; (3) a
`pivy` wheel through `make_wheel.py`, loaded beside `fcx_image`; gate:
`from pivy import coin; coin.SoDB.getVersion()` in the guest, a 10 k
node graph built and traversed by `SoSearchAction` and `SoGetBoundingBoxAction`
(no GL action), timed; then 7.9's probe re-run -- Draft's `Initialize()`
passes its self-test and reaches `import DraftTools`.  Sizes and boot
cost are the numbers to record.  The fallback if (1) or (2) fails on
something structural is 7.2's Coin-shaped model library.

**PROBE A PASSED 2026-09-05, the same day, on the first complete run
of each step.**  (1) The Coin fork gained `COIN_BUILD_GL_STUB` (commit
`6a79416a69` on `LinkVibe`): an `elseif` before the OpenGL lookup that
sets `HAVE_OPENGL` and nothing else, and `src/glue/gl_stub.cpp`, 420
core `gl*` entry points generated by `gen_gl_stub.py` from the
toolchain's `GL/gl.h` (vendor-suffixed and function-pointer-typed
declarations skipped; `glGetString` answers "1.1 stub", the `glGet*v`
family writes 0).  Configured with `emcmake` -- static, GLX/EGL/sound
off, Boost headers from `.conda/freecad/include`, `-fPIC
-fwasm-exceptions -sSUPPORT_LONGJMP=wasm` -- every one of the 464
real Coin sources compiled unchanged; `libCoinRT.a` is 11.8 MB.  (2)
`src/App/PyodideHost/pivy/CMakeLists.txt`: the host's SWIG 4.4.1 over
pivy's `coin.i` (17.8 MB of wrapper, 2.0 MB `coin.py`), the wrapper
compiled with the guest flags plus `PY_CALL_TRAMPOLINE`, Coin linked in
statically, `-sSIDE_MODULE=2` exporting `PyInit__coin`; `_coin.so` is
13.7 MB, the wheel 2.7 MB.  (3) `Layout::bundledCompiled`: a compiled
wheel beside the pure ones is bundled per (ABI, distribution) and the
runtime loads those of the running fcx_image's ABI after the pure
wheels; `FREECAD_PIVY_WHEEL` ships and mirrors it like the fcx_image
wheel.  Measured by `scripts/sandbox-pivy-probe.py` in the guest:

    import pivy.coin           0.55 s   Coin "SIM Coin 4.0.6rt", 649 SoTypes,
                                        1554 names in the module
    10 000 separators          0.77 s   30 001 nodes, SoTranslation + SoCube each
    SoSearchAction, all cubes  0.013 s  10 000 paths
    SoGetBoundingBoxAction     0.028 s  the right box
    guest boot                 +0.2 s   1.86 s -> 2.09 s with the wheel bundled

So the answer to 7.2's question is REAL COIN in the guest, not a
Coin-shaped model library; the mirror (G4) reads real `SoNode`s.
Draft's `Initialize()` self-test then reached `FreeCADGui.getSoDBVersion()`
(added to the guest module as `gui.sodb_version`) and, past it,
`import DraftTools` -- the same wall BIM hits, which is G3's ("Initializing
one or more of the Draft modules failed", caught by Draft itself, after
which its `Initialize` raises at `FreeCADGui.getMainWindow()`, so the
probe no longer activates from the host by default).  Traps:
the Emscripten toolchain confines `find_library` to its sysroot (name
the archive); the console binary does not flush `stdout` at exit (a
probe prints to stderr); `SoPathList` has no `len()`.

### 7.11 G3 sized: the forms, Qt-shaped **[sized 2026-09-05]**

G3 is U3 over the widget protocol (7.3) for the code as it is written:
`Gui.PySideUic.loadUi(":/ui/X.ui")` then `self.form.<name>.<Qt method>`
(41 panels), and forms built in code from `QtWidgets.QLabel(parent)`,
`QHBoxLayout()`, `layout.addWidget(w)` (DraftGui's toolbar, ArchPrecast,
ArchComponent, ArchWindow).  What the linter counts as U3 (1504 uses)
is, by chain: `SIGNAL`/`QObject.connect` 285, the widget constructors
(`QLabel` 94, `QPushButton` 86, `QWidget` 49, `QHBoxLayout` 49,
`QComboBox` 35, `QVBoxLayout` 29, `QLineEdit` 27, `QCheckBox` 24,
`QGridLayout` 21, `QDoubleSpinBox` 14, `QFormLayout` 13, `QSpinBox` 11,
`QGroupBox` 10, `QRadioButton` 7), the item models (`QStandardItem` 88,
`QTreeWidgetItem` 47, `QTableWidgetItem` 20, `QListWidgetItem` 12,
`QStandardItemModel` 9, `QStyledItemDelegate` 20), `QAction` 76 and
`QToolBar` 24 and `QMenu` 7, `Gui.Control.*` 124, `Gui.getMainWindow`
56, `loadUi` 41, `UiLoader().createWidget` 29 (all `Gui::InputField`
and `Gui::ToolBar`), `Gui.draftToolBar.*` 60.  The methods the panels
call on a form's widgets, by count: `text` 82, `setText` 80,
`isChecked` 72, `setProperty` 68 (`rawValue` on an InputField), `value`
58, `setEnabled` 49, `setValue` 45, `clicked` 44, `property` 38,
`currentIndex` 36, `setChecked` 34, `setCurrentIndex` 29, `addItem` 23,
`hide` 22, `setIcon` 21, `currentIndexChanged` 20, `selectedItems` 19,
`stateChanged`/`checkStateChanged` 34, `pressed` 16, `show` 11,
`expandAll` 11, `currentText` 11.  The `.ui` files (68) use 41 widget
classes: QLabel 409, `Gui::PrefCheckBox` 136, QPushButton 118,
QGroupBox 93, QCheckBox 51, QComboBox 45, `Gui::InputField` 45,
QWidget 44, `Gui::PrefLineEdit` 42, QDialog 32, `Gui::PrefComboBox` 32,
`Gui::ColorButton` 31, QSpinBox 29, QLineEdit 28, QDialogButtonBox 26,
`Gui::PrefSpinBox` 26, `Gui::QuantitySpinBox` 21,
`Gui::PrefColorButton` 21, `Gui::PrefDoubleSpinBox` 20, QRadioButton
15, `Gui::PrefUnitSpinBox` 15, then the trees, lists and tables (QTreeView
10, QListWidget 10, QTreeWidget 7, QTableWidget 2) and singletons.

**The decision: one model per Qt class, Qt's property names as the
state.**  Probe B put UNMODIFIED ipywidgets in the guest and rendered
its core models; the corpus does not speak ipywidgets, it speaks Qt,
and the core models fit it badly (a `QPushButton` is a Button or a
ToggleButton by a runtime `setCheckable`; a `QLabel` has no `enabled`;
every DOMWidget drags a Layout and a Style model, three comms a widget,
where a form is sixty widgets).  So the FreeCAD widget module 7.3
called for is the Qt subset itself: a guest package `freecad.widgets`
(the fcx_widgets wheel; `freecad` is a namespace package the bundled
wheels share -- they all `loadPackage` into one site-packages) of
`ipywidgets.Widget` subclasses (`Widget`, not `DOMWidget`: ONE comm a
widget, no layout/style models) with `_model_module = "freecad.widgets"`
and a model per Qt class -- `QWidgetModel`, `QLabelModel`,
`QPushButtonModel`, `QToolButtonModel`, `QCheckBoxModel`,
`QRadioButtonModel`, `QLineEditModel`, `QTextEditModel`,
`QSpinBoxModel`, `QDoubleSpinBoxModel`, `QComboBoxModel`,
`QGroupBoxModel`, `InputFieldModel` (also `Gui::QuantitySpinBox`),
`ColorButtonModel`, `UiFormModel` -- whose synced traits ARE the Qt
properties (`text`, `checked`, `enabled`, `visible`, `toolTip`,
`value`, `minimum`, `maximum`, `singleStep`, `decimals`, `items`,
`currentIndex`, `rawValue`, `unit`, `color`, ...), so a `.ui`
`<property>` is a state key and the host view's `apply` is a
property-by-property `setX`.  The Qt-flavored accessors (U7) are the
methods of those classes: `setText`/`text`, `setChecked`/`isChecked`,
`setValue`/`value`, `setProperty`/`property`, `show`/`hide`/
`setVisible`/`isVisible`, `setEnabled`/`isEnabled`, `setToolTip`,
`addItem`/`addItems`/`clear`/`count`/`itemText`/`currentText`/
`setCurrentIndex`, `setObjectName`/`objectName`, `findChild`,
`setWindowTitle`, `setWindowIcon`, `setFocus` (a custom message),
`selectAll`, `setStyleSheet` and the size hints (accepted, most
ignored).  The guest classes `PySide.QtWidgets.QLabel` and the rest
ARE those models, so the corpus constructs them unchanged; the
`Gui::Pref*` classes are their base class in the guest plus
`prefEntry`/`prefPath`, and the HOST makes the real `Gui::Pref*`
widget (through uic for a `.ui`, `UiLoader().createWidget` for a built
one), so the preference reading and saving stays native.  The core
ipywidgets models and their views stay for scripts that use them.

**Signals.**  `PySide.QtCore.Signal` is a descriptor yielding a
per-instance signal with `connect`/`disconnect`/`emit`; `SIGNAL("x()")`
is the name and `QObject.connect(obj, sig, fn)` is
`getattr(obj, name).connect(fn)` (the 285 old-style uses).  A value
signal is a trait observer emitting with Qt's argument
(`stateChanged(2)` for checked, `currentIndexChanged(i)`,
`valueChanged(v)`, `textChanged(s)`), so a programmatic `setChecked`
fires it exactly as Qt does; an event signal (`clicked`, `pressed`,
`returnPressed`, `editingFinished`, `textEdited`) is a custom message
`{"event": name, "args": [...]}` from the host view.  A radio button's
exclusivity is native on the host and mirrored in the guest among
siblings of one parent.

**`loadUi` on both sides.**  The guest's `FreeCADGui.PySideUic.loadUi(
path)` reads the file's text through `gui.ui.read` (the host's Qt
resource system or a file under the module roots: data, not code),
parses it with `xml.etree` (every `<widget class= name=>`, its
`<property>`s and `<item>`s), builds the model of each class (an
unknown class is a `QWidgetModel`) and a `UiFormModel` root carrying
`uiFile` and `widgets {name: ref}`, each named widget an attribute of
the root, as uic does.  The values parsed from the file are the
model's initial state but NOT the host's: a model lists in `_touched`
the properties the guest SET (a setter, a constructor argument), the
file's values are written silently, and the host's `UiFormView` loads
the same file through `FreeCADGui.UiLoader().load` (Qt's uic: the
layout exact, the strings TRANSLATED, the custom widgets real) and
BINDS each named child widget to its model -- the same view class,
adopting the existing widget instead of building one -- applying the
touched properties only; a built widget likewise gets the touched
ones and keeps Qt's defaults for the rest (a default `prefPath` of ""
applied to an InputField throws).  A view therefore has two entries,
`build(parent)` and `bind(widget)`.

**The task panel.**  `FreeCADGui.Control.showDialog(panel)` registers
the guest panel as a proxy with the panel hook list (`accept`,
`reject`, `clicked`, `open`, `getStandardButtons`,
`modifyStandardButtons`, `needsFullSpace`, `isAllowedAlterDocument`,
`isAllowedAlterView`, `isAllowedAlterSelection`, `helpRequested`,
`shouldShow`; `isHookName` learns them) and crosses
`gui.control.show [descriptor, form ids]`; the host builds a
`GuestTaskPanel` whose `form` is the rendered forms' widgets and whose
hooks forward to the stand-in (the standard buttons an int of Qt's
values, which the shim's `QDialogButtonBox` enum carries), and shows
it through the native `Control.showDialog`.  `closeDialog`,
`activeDialog`, `clearTaskWatcher` are one op each.  `showWidget`
stays for plain ipywidgets.

**In stages, each with its gate:**

- **G3a** -- the models above for the plain Qt classes plus
  `Gui::InputField`/`Gui::QuantitySpinBox` and `Gui::ColorButton`, the
  signals, `loadUi` on both sides, the task panel bridge.  Gate
  `SandboxForms`: a probe in the guest loads Draft's
  `TaskPanel_OrthoArray.ui` (9 InputFields, 3 spin boxes, 3 check
  boxes, 3 radio buttons, 4 buttons, 4 group boxes), sets fields
  Qt-style, connects old- and new-style signals, shows it as a task
  panel; the host's uic form is the active dialog with the guest's
  values in the bound widgets and a `.ui` default left untouched; a
  host edit of each kind reaches the guest state and its Qt-named
  signal with Qt's argument; OK crosses `accept` where the guest reads
  every field back; and Draft's real `task_orthoarray.py`, exec'd
  unmodified in the guest, constructs, shows, and answers
  `get_numbers()`/`get_intervals()` from the bound form.
- **G3b** -- the rest of the `.ui` subset: `QDialog` with
  `QDialogButtonBox` and a synchronous `exec_()` (a nested Qt event
  loop on the host inside one op; the guest's callbacks nest as the
  slider already does), the tree/list/table family with typed items
  (`QTreeWidgetItem`, `QStandardItem`, `QListWidgetItem`, delegates as
  cell types), `QTabWidget`/`QStackedWidget`/`QScrollArea`/`QSplitter`
  as containers, `Gui::FileChooser` (an `fs` grant), `QFontComboBox`,
  `QTextBrowser`, `QProgressBar`.  Gate: every one of the 41 `loadUi`
  panels of Draft and BIM opens from the guest and round-trips its
  fields (a harness over the list, the way the reopen gate walks the
  corpus).
- **G3c** -- forms built in code: the layout classes as a widget's
  `layout` state (`{type, items}` nested, re-synced on mutation), the
  `QWidget` container, `QSpacerItem`/`QSizePolicy` accepted, `QAction`/
  `QToolBar`/`QMenu`/`QDockWidget`, `QFont`/`QColor`/`QIcon`/`QPixmap`
  as data, `getMainWindow()` as a shim (`addToolBar`,
  `mainWindowClosed.connect`, `addStatusBarItem`), `DraftLineEdit`'s
  `keyPressEvent` and `DraftBaseWidget`'s `eventFilter` as a `keys`
  custom event stream the host sends for the widgets that ask.  Gate:
  `DraftGui` imported in the guest, `draftToolBar.taskUi()`/`lineUi()`
  shown from the guest and `validatePoint` round-tripping the point.
  This is the wall both `Initialize()`s stop at (7.9), so the G2b
  runner follows it.
- **G3d** -- the selection input (U4's `Selection` as a model) and the
  U2 dialogs (`QMessageBox.question`, `QInputDialog.getText`,
  `QFileDialog.getOpenFileName`, `QColorDialog.getColor`: one
  synchronous op each, the static calls the subset already names).

Cost, expected: one comm per widget (a sixty-widget form about 60
opens at 0.5 ms), the panel's construction in the guest, one hop per
host event; measured at G3a.

**G3a BUILT 2026-09-06**, the gate green on the fourth complete run
(`SandboxForms`, 3 cases; all seven GUI gate cases OK).  What the
build settled beyond the sizing:

- **The traits are `q_<property>`.**  A trait named `text` would
  shadow Qt's getter `text()` on the same object (the corpus calls
  `w.text()`, `w.value()`, `w.icon()`, `w.color()`), so every Qt
  property is the trait `q_text`, `q_value`, ...; the host strips the
  prefix and calls Qt's own `setProperty`, with handlers for what is
  not a Q_PROPERTY (a combo's items and icons, an icon path, a color
  as four floats).  `setProperty("rawValue", v)` and `property(...)`
  map by name.
- **`_touched`, not `_uiDefaults`.**  The class default is as wrong to
  apply as the file's value: `prefPath = ""` applied to an InputField
  throws in `GetParameterGroupByPath`, an empty `toolTip` would clear
  uic's.  So a model lists the properties the guest SET (setters,
  constructor arguments), the `.ui` loader writes the file's values
  silently, and a view applies the touched properties at bind/build and
  every key of a later update (equal or not: a text the guest set that
  the file already had still overrides uic's translation of it).
- **Layouts came forward from G3c.**  Draft's `task_orthoarray.py`
  rearranges its form at construction (`group.layout().takeAt(0)`,
  `item.widget().setParent(None)`, `grid.addWidget(w, r, c, 1, 2)`,
  `form.findChild(QtWidgets.QGridLayout, "grid_number")`), so the
  layout classes exist in the guest as plain objects on their widget
  (`QVBoxLayout`, `QHBoxLayout`, `QGridLayout`, `QFormLayout`, items
  and spacers), the `.ui` parser builds them with their names, and a
  mutation crosses as a layout op on the OWNING widget's comm
  (`{"layout": name, "op": addWidget|insertWidget|removeWidget|takeAt|
  addLayout|addStretch|addSpacing|setContentsMargins|setSpacing, ...}`)
  that the host applies to the real layout of that name under that
  widget (a `.ui` file reuses names -- OrthoArray has two
  `gridLayout_5` -- so the search starts at the owner).  A widget the
  guest made in code and adds to a layout gets a view BUILT under the
  layout's widget then (`QLabel(translate(...))` in linear mode).
  `setParent` crosses as an event (`None` hides, as Qt's does).  A
  layout with no name reaches no host layout: code-built forms are
  still G3c.
- **`prefs.write`** joined the catalog (2.2): the panel's first act is
  `params.set_param("LinearModeOn", ...)`, and DraftGui's ContinueMode,
  the 83 `set_param` sites of Draft's GUI side and every `Pref*` widget
  write the store.  DENY for a document (not promptable, like `gui`),
  ALLOW for the session and addons; the guest's `ParamGet(...).Set*` /
  `Rem*` now cross through `freecad.prefs.write`/`remove` instead of
  warning once, and `draftutils.params.set_param*` are a second facade
  entry on the same guest module (the generator merges entries by
  module: one permission per entry).
- **`FreeCADGui.ActiveDocument`**, the first U4 op: every Draft
  panel's `finish()` is `Gui.ActiveDocument.resetEdit()`; the guest
  gets a `GuiDocument` (`resetEdit`, `Document`) while the host has an
  active document, None otherwise; `setEdit`/`getObject` wait for U4.
- **The host's `Control.activeDialog()` is a bool** in this fork (as
  its `showDialog` returns None), so a gate drives OK/Cancel through
  the manager's panel object; `TaskDialogPython::reject` reading None
  as False is what Draft relies on (its `finish()` closes the dialog
  itself through the command's `completed()`).
- Facts of the run: a `Manager.reset()` between cases must keep the
  dispatcher (the guest is the same one; a real reset re-registers);
  `faulthandler` in the gate script needs `sys.__stderr__` (FreeCAD's
  console has no fileno) and the runner prints a failure's traceback
  as it happens, since the crash that followed (a `setParent` on a
  uic child whose form the failed bind had let go) took the buffered
  report with it -- a failed bind now deletes the form itself; the
  guest's `Quantity.UserString` has no decimals setting ("120 mm" to
  the host's "120.00 mm"), so an InputField's text differs between the
  two until a host edit sends the host's -- both parse to the same
  value, which is all the corpus does with it (13).

Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`):

    TaskPanel_OrthoArray.ui loaded in the guest,   0.084 s: 40 models, 91 comm
      5 fields set, shown as a task panel           ops, 1 gui.ui.read, 1 control.show
    Draft's TaskPanelOrthoArray() + show           0.067-0.094 s: 127 comm ops (the
      (params, linear-mode re-layout included)       layout ops among them), 12 mod_call
    host spin-box edit -> guest state + signal     0.19-0.21 ms each
    guest setValue -> Qt widget                    0.09 ms each

Gate `SandboxForms` (3 cases): the probe form from the .ui with its
values in uic's widgets and the file's own left alone, a hundred host
edits and a hundred guest sets, every edit kind reaching its Qt-named
signal with Qt's argument, OK reading every field back, Cancel,
close from the guest; Draft's `task_orthoarray.py` unmodified --
constructed (linear mode re-layout included), shown, `get_numbers()`/
`get_intervals()` answered from host edits, its checkbox callback,
its reject; a document principal refused at `loadUi`.  Suites after:
the three GUI gates 7/7; `Tests_run --gtest_filter='Expression*'`
pyodide 104 passed + 1 skipped, wasi 31 + 74 skipped on this box.

### 7.12 The host widget layer sized: native task panels on the same models **[sized 2026-09-06]**

The question, asked at the end of the G3a session: replace the Qt
task panel with imgui through the same API translation the guest
uses, for native code too.  Answered in three rulings the same day,
each on a sizing: imgui out on the desktop, DOM in the browser, and
the host abstraction itself KEPT -- two backends, desktop first.

**What is there.**  The vocabulary is small and already toolkit-
neutral: 22 model classes, about 60 `q_` properties, 9 layout ops,
the custom events (`setFocus`, `selectAll`, `setParent`), 12 panel
hooks; only the consumer, `freecad.widgets.qt` (1121 lines), is
Qt-specific.  Dear ImGui 1.92.8 WIP is vendored under bgfx and, since
`BGFX_BUILD_TOOLS` forces `example-common` on, already compiled and
linked into `FreeCADRenderer` with bgfx's glue, nanovg and the SDF
font code -- unused, harmless, left alone; the WASM build leaves it
out.  The renderer has an overlay pass block fed with geometry, no
text path in the 3D tier, and no input path at all (events go Qt to
Quarter to Coin); Qt owns the GL context and bgfx blits into Qt's
framebuffer.  The browser viewer renders locally from the scene
stream and its chrome is SolidJS over the canvas.

**The native corpus** (counts from the tree at `f9084a4ee6`):

    TaskDialog subclasses / TaskBox subclasses      93 / 35
    lines in the 90 dialog units                    ~60,000
    TaskView framework + Control                    ~5,500
    dialogs driven by a uic'd Ui_* header           74 of 90
    units with item views (tree/list/table)         20
    expression bind( sites                          261 in 40 files
    units with SelectionObserver / gate / QTimer    17 / 14 / 12
    C++ Control().showDialog call sites             171 (56 from setEdit)
    Python Control.showDialog / loadUi sites        109 / 198
    .ui files: 511, uic-compiled 365, Python-loaded 142

    custom widgets the forms depend on             lines
    QuantitySpinBox + ExpressionBinding + label    ~1,400
    DlgExpressionInput + ExpressionCompleter       ~3,500
    PrefWidgets (18 classes)                       ~2,300
    Widgets.cpp (ColorButton, ActionSelector, ...) ~3,000
    InputField, FileChooser, DlgPropertyLink       ~3,500

By module the dialog code is PartDesign 14.8k, Fem 12.7k, TechDraw
10.5k, Part 7.3k, Sketcher 4.8k, Surface 3.4k, Gui 2.0k, then Drawing,
Robot, Mesh, Material.  Draft, BIM, Assembly, Spreadsheet, CAM have
no native TaskDialog.  What makes the hard panels hard is mostly NOT
widget code: selection observers and gates, document observers,
timers, Coin callbacks sit on QtCore and App and survive a widget
swap untouched.  What moves is the form, the item views and the
custom widgets' presentation.  The outliers: Sketcher's constraint
and element lists (custom item widgets, hover to 3D), Fem's
`TaskPostBoxes` (2383 lines, VTK pipelines), Part's `TaskDimension`
(Coin decorations), `TaskAttacher`, PartDesign's `TaskMultiTransform`
(nested sub-dialogs) and `TaskHoleParameters`, `DlgPropertyLink`,
Drawing's `TaskOrthoViews`.

**imgui, and why not.**  On the desktop it cannot give back
accessibility (none upstream, none planned), the stylesheet look, or
rich text; focus, nested `exec()`, native dialogs, timers and
observers would all have been fine, since Qt keeps the event loop.
It would have cost a 3-5k walker, a 1k input/IME bridge and a panel
host, for unification with tiers that do not exist yet.  In the
browser, pure DOM against imgui-with-a-hidden-input: the walker is
1.5-2k of TypeScript beside the existing inspector and ships nothing;
imgui needs the walker plus 1.2-1.6k of browser-only bridge (pointer,
the hidden input under the caret, touch to mouse, kinetic scroll,
clipboard, arbitration with the viewer's navigation), about 1 MB of
wasm plus shipped fonts (CJK a subset or a multi-MB TTF, wasm has no
system fonts), redraws while a caret blinks, a second visual system
on the page, and pixels for tests.  The hidden input fixes entry into
a focused field, with upstream focus/backspace issues open, and
nothing else: not scrolling physics, accessibility, wrap and reflow,
or rich text; the EditContext API that would make it proper is
Chromium-only with no Emscripten binding.  A task panel is mostly
typed fields (`Gui::QuantitySpinBox` is the most common `.ui` control
after labels, and a quantity field is a text field with a unit
parser), so the case imgui was built for is the opposite of this one.

**The host layer's shape.**

- **The core is a property bag.**  Each host widget class is a QObject
  in its own namespace with Qt's class and method names kept
  (`Fw::QLabel::setText`, as the guest's `PySide.QtWidgets.QLabel` IS
  the model), a variant map keyed by Qt's property names, a dirty set,
  the layout op log, and moc'd signals with Qt's names and arguments:
  traitlets in C++.  A backend is a consumer of the bag and nothing
  else; whatever the DOM will need must be in the bag.  That is the
  one design rule.
- **Our own `.ui` generator, not uic.**  On the guest's `uic.py`
  parser, a generator emits a C++ `Ui_X::setupUi` that instantiates
  the host classes with their names, layouts and the file's values,
  so a ported dialog keeps `ui->lengthEdit` as a typed member and its
  `connect` calls compile unchanged.  A ported translation unit never
  includes QtWidgets, so there is no name ambiguity.
- **The Qt backend reuses G3a wholesale.**  For a `.ui` form the C++
  Qt view loads the same file through `UiLoader` and binds each named
  child to its host object, as `UiFormQtView` does for the guest:
  uic's layout exact, strings translated, `Gui::Pref*` and the
  quantity widgets real; layout ops cover mutations; no layout
  fidelity code on the Qt side at all.  Binding writes uic's
  translated strings back into the bag, which is what gives the DOM
  tier translated text later for free.
- **Custom widgets split on a seam that exists.**  `ExpressionBinding`
  is already a non-widget mixin, so the host `QuantitySpinBox` mixes
  it in and owns the binding, while the real `Gui::QuantitySpinBox`
  stays the Qt presentation with its f(x) label and expression dialog.
  Pref widgets are two properties in the bag and the real widget on
  the Qt side.
- **One store for both producers.**  A guest comm open creates the
  same host object by model name, so the C++ Qt view renders guest and
  native panels alike and the Python Qt manager retires; the plain
  ipywidgets views of Probe B may stay in Python behind the same
  `gui.comm` dispatch, split by model module.
- **The task panel framework** keeps `TaskDialog` and its hooks
  (already non-widget) and gains a `TaskBox` model; `Control` and
  `TaskView` are untouched, a realized root being a QWidget to them
  exactly as `GuestTaskPanel` is now.
- **A second backend costs one consumer**, not a second design: the
  C++ Qt view is the port of the Python manager (2.5-3.5k), every
  vocabulary addition then lands once per backend, and gates run per
  backend.  The DOM walker (G7) is 1.5-2k of TypeScript, the manager
  beside the guest in the browser's worker when the panel is a guest
  one, a remote view over the socket when it is native on a serving
  host -- the thin-client model already.

**Stages and gates.**

    H0  the bag, signals, the generator, the class set the first two
        targets need, the C++ Qt view with UiLoader binding, layout
        ops, task panel integration.  Gate: OrthoArray from the guest
        renders through the C++ store, SandboxForms green, the Python
        Qt manager off for freecad.widgets models.
    H1  first native ports: one form-only panel in src/Gui
        (TaskAppearance or TaskOrientation), then a PartDesign panel
        with expression binding (Pad).  Gate: the ported panel drives
        its property, bind and f(x) round-trip, existing suites green.
    H2  the form-only majority, about 60 units, mechanical.  Gate:
        each module's tests plus a per-panel open and round-trip
        harness.
    H3  item views (tree/list/table items, model-backed views), then
        the nine outliers, Sketcher's lists last.

**Cost, desktop only** (lines, new or rewritten):

    property core, signals, the 41 classes' API subset      6-9k
    generator (Python, on uic.py)                           0.5-1k
    C++ Qt view, binding, layout ops, item views            3-4k
    task panel integration, guest store switch              ~2k (retires 1.4k Python)
    custom widget seams (quantity, expression, pref, ...)   1-2k
    dialog ports: 60 light, 29 heavy                        10-15k touched
    total                                                   25-30k

Every line serves the DOM tier later.  Out of scope even then: the
property editor (8.4k), the tree view, preference pages, and the 154
`QDialog` forms -- Qt stays the application shell.  The imgui route
would have added a 3-5k walker, the input bridge and a host, for a
30-40k total; that number is kept here as the record of why it lost.

**Ordering.**  G3b's class set (dialogs, the tree/list/table family,
containers, the file chooser) is H3's; done G3b-first its views would
be written in Python and re-ported.  So H0 and H1 go before G3b, and
G3b then lands on the C++ store.  The G2b runner waits on G3c either
way.  Next step: H0, starting with the property core and the
generator, gated on OrthoArray through the guest before any native
port -- that gate already exists.

**H0 BUILT 2026-09-06**, gate green: OrthoArray from the guest renders
through the C++ store and the C++ Qt view, `SandboxForms` 3/3 with the
Python Qt manager holding no `freecad.widgets` model, the seven GUI
gate cases OK.  What is there, in `src/Gui/Fw/` (4.7k lines, namespace
`Gui::Fw` for the toolkit-neutral half, `Gui::FwQt` for the Qt
consumer):

- **`FwCore`** -- `Fw::Widget`, a QObject whose state is a QVariantMap
  under Qt's property names with declared defaults, a touched set, and
  the signals `propertiesChanged(names, source)`, `requested(name,
  args)` (model to backend: `setFocus`, `selectAll`, `setSelection`,
  `setCursorPosition`, `setParent`), `eventEmitted(name, args)`
  (backend to model: `clicked`, `pressed`, `editingFinished`, ...) and
  `layoutChanged(op)`.  Three writers, one rule each: a typed setter
  (`Source::Native`) marks touched; a backend (`Source::Backend`)
  reporting the user does not; the guest (`Source::Guest`) does not
  either -- it syncs its own `_touched` list, and marking on its
  behalf was the one bug the gate caught (a `.ui` value the guest's
  loader wrote silently came back touched, since traitlets does not
  resend an unchanged `_touched = []`).  `propertiesChanged` names
  every key written, equal or not; the typed value signals fire on a
  real change only, whichever side made it.  `property`/`setProperty`
  HIDE QObject's, since Qt's would read a Q_PROPERTY of the model
  object instead of the bag.  `Fw::Layout` holds items and positions
  and emits every mutation as an op on the OWNING widget (through
  parent layouts), the guest's payload shape exactly, with the widget
  as a `QObject*` instead of a comm id; an unnamed layout emits
  nothing.  The QObject tree IS the model tree: `findChild` works,
  radio exclusivity walks siblings, `setParent` requests the backend
  to move the realized widget.
- **`FwWidgets`** -- the 22 classes with Qt's methods and moc'd
  signals (`Fw::QSpinBox::valueChanged(int)`, `Fw::QCheckBox::
  stateChanged(int)`, `Fw::InputField::valueChanged(double)`, ...),
  `Fw::UiForm` (the file and its named widgets), and the factory
  `createWidget(name)` keyed by Qt class name as a `.ui` spells it OR
  by guest model name (`Gui::PrefCheckBox` -> `Fw::QCheckBox` with
  `qtClass` kept; `InputFieldModel` -> `Fw::InputField`).  An
  `InputField::setValue` formats its `text` in the unit schema at the
  user's decimals, so the bag carries what a DOM tier would show.
- **`FwStore`** -- the guest's objects by comm id: `commOpen` creates
  by `_model_name`, writes the `q_` state silently, takes `_touched`,
  `qtClass`, `uiFile` and the `widgets` map; `commUpdate` is
  `setProperties(Guest)`; `commCustom` is a request, a `setParent`
  (ids resolved to objects) or a layout op relayed; `commClose`
  deletes (children the guest re-parented under it are unparented
  first, their comms being open).  The way back is a sink: a
  non-guest `propertiesChanged` goes out as an `update` with `q_`
  keys, an `eventEmitted` as a `custom`.  The store knows neither
  Python nor Qt.
- **`FwQtView`** -- the port of the Python Qt manager's Qt-shaped
  half.  One `View` per realized model; `build` makes the real widget
  of `qtClass` (FreeCAD's through the widget factory, Qt's through a
  cached `UiLoader`), a `UiForm` loads its file through the same
  loader and binds each named child; `bind` READS the widget back into
  the bag silently first (every bag key the widget has as a
  Q_PROPERTY, `visible` excepted, colors as four floats, enums as
  ints), then applies the touched keys.  `apply` orders range before
  value, items before index, drops `text` beside `rawValue`, and
  otherwise calls Qt's `setProperty`; the Qt signals are connected by
  the REAL widget's type (`qobject_cast` down a fixed list), writing
  the bag from `Source::Backend` under an `applying` guard so nothing
  echoes.  A view lives on the model, watches the widget's `destroyed`,
  and `release`s when a dialog deletes its content.
- **`FwQtPanel`** -- `PanelDialog`, a `TaskDialog` whose content is
  the realized forms and whose hooks go to a `PanelHooks` interface;
  `SandboxGui.cpp` implements it over the guest's stand-in (the
  descriptor's hook list decides `has`, a None answer reads False as
  TaskDialogPython does).  `TaskView` never learns anything new.
- **`FwPy`** -- `FreeCADGui.FormWidgets`: `ids`, `count`, `info(id)`
  (class, qtClass, properties, touched, bound, the form's map),
  `find`, `resolve`, `widget(id)` (the PySide wrapper of the realized
  widget), `setProperty`, `stats`, `reset`, and `accept`/`reject` --
  the active `PanelDialog`'s buttons, needed because this fork's
  `TaskView` never hands a dialog its button box, so
  `Control.activeTaskDialog().accept()` clicks nothing (the gate's
  first false alarm).  Plus the QVariant <-> PyObject conversions the
  bridge shares.
- **`src/Tools/fwuic.py`** (0.4k) -- the generator: `Ui_X::setupUi(
  Fw::UiForm*)` with typed members (`Gui::Fw::InputField* input_X_x`),
  the file's layouts by name (a reused name gets `_2`, as uic does),
  values through `setInitial`, `<item>`s as a combo's lists, spacers
  and positions; the parse rules are the guest loader's, and the
  guest's `qtdata.Qt` supplies the enum values.  `quantity` maps to
  `rawValue`.  Strings are emitted untranslated: the Qt backend reads
  uic's translations back at bind time, and the DOM tier's
  translation is its own question.  The header is named `fwui_X.h`,
  since CMake's AUTOUIC claims every `ui_*.h` include for uic.
- **The bridge** (`SandboxGui.cpp`): a `jupyter.widget` comm whose
  opening state says `_model_module == "freecad.widgets"` goes to the
  store, and every later message on an id the store holds; the plain
  ipywidgets models of Probe B still go to the Python manager, so
  `SandboxWidgets` is untouched.  `gui.control.show` builds a
  `PanelDialog` when every form id is the store's (else the Python
  `show_panel`); `close`/`active`/`query` run in C++ against
  `Control()` and the active Gui document.  `freecad.widgets.qt` lost
  its Qt-shaped views (402 lines); `GuestTaskPanel` stays for plain
  ipywidgets roots.

Measured (RelWithDebInfo, Xvfb, the same gate):

    TaskPanel_OrthoArray.ui loaded in the guest,   0.077 s: 40 objects opened, 50
      5 fields set, shown as a task panel           updates, 91 comm ops (was 0.084)
    Draft's TaskPanelOrthoArray() + show           0.074 s: 42 objects, 75 updates, 10
                                                     customs (layout ops), 127 comm ops
    host spin-box edit -> guest state + signal     0.16-0.20 ms each (was 0.19-0.21)
    guest setValue -> Qt widget                    0.05-0.06 ms each (was 0.09)

Tests: `tests/src/Gui/FormWidgets.cpp` (11 cases, QTest, offscreen: the
bag's defaults, touched and signal semantics, coercion, radio
exclusivity, combo items, layout ops reaching the owner and not from
an unnamed layout, the factory, the generated OrthoArray form's names
and values, the Qt view binding uic's form from the file path -- read-
back, touched applied over uic's, both directions, a layout op on the
real layout, a request, detaching when the widget dies -- and a built
widget).  A fact it recorded: uic writes the file's `quantity` double
into `Gui::InputField`'s `Base::Quantity` property, which does not
take, so the real widget shows 0 and the bag, mirroring the widget,
does too; the generated form holds the file's 100 until bound.
Suites after: the GUI gates 7/7, FormWidgets 11/11, `Tests_run
--gtest_filter='Expression*'` 104 passed + 1 skipped.

**H1a BUILT 2026-09-06**: the first native port, `TaskOrientation`
(`src/Gui/TaskView/`).  Neither candidate is reachable from the app
any more -- the Image module that opened `TaskOrientationDialog` was
removed from this fork, and `TaskView` stopped adding `TaskAppearance`
long ago -- so the port is the mechanics proof and its gate is a test.
The panel is now a QObject over `Fw::UiForm` plus the generated
`Ui_TaskOrientation` (`fwui_TaskOrientation.h`, `fc_wrap_fwui` in
`cMake/FreeCadMacros.cmake`, the .ui in `src/Gui/Fw/fwui.qrc` under
`:/ui/`); its five `connect`s compile unchanged against the models'
signals; the dialog realizes the form through `FwQt::realize` into
its `TaskBox`, and the unit includes no QtWidgets.  What the port
needed and the layer gained: `<size>` properties as the width/height
bag keys (`minimumSize`, `maximumSize`), in the generator and the
guest loader alike; a grid item's `colspan` without `rowspan` (both
readers dropped the span); the generated class named from `<class>`
inside its namespaces, as uic does (`Gui::Ui_TaskOrientation`, plus
the `Ui::` alias); and a pixmap path scheme, `bitmap:<name>`, the Qt
backend renders through the BitmapFactory at the label's size (the
preview icon; the bag carries a name a DOM tier can resolve).  Gate
`test_taskOrientationPort` in `FormWidgets_Tests_run` (12/12): the
file's title reached the model translated, `open()` restores the
placement into the models and the widgets follow, the widget drives
the property (offset, plane, reverse), the model drives the widget,
accept; the GUI gates stay 7/7.

**H1b BUILT 2026-09-06**: Pad and Pocket's panel, with the expression
seam.  Their shared form `TaskPadPocketParameters.ui`, owned by
`TaskExtrudeParameters` (245 `ui->` uses, 10 `bind(` sites, seven
`Gui::PrefQuantitySpinBox`, three `Gui::DoubleSpinBox`), is now the
models fwuic generates (`fwui_TaskPadPocketParameters.h`, the .ui in
`Resources/PartDesign.qrc` under `:/ui/`); `ui->lengthEdit` is a typed
`Gui::Fw::QuantitySpinBox*` and the file's 245 uses, its `connect`s
(retyped to the models' signal classes) and its two grid connections
compile unchanged.  `TaskSketchBasedParameters`'s code-built widgets
(the profile edit, `LinkSubListWidget`, the fitting group, the
operation combo) stay Qt: the port is the FORM, and the surrounding
`TaskBox` is realized into the same `proxy` by `FwQt::realize`.  The
Qt-only calls the panel keeps -- an event filter, a `ButtonGroup`, the
blink target, the select-reference label, `hookPropertyBool` -- reach
the real widget through `FwQt::widgetOf(model)`.

The seam, the sizing's `ExpressionBinding` on the host models:
`Fw::ExpressionBound` mixes `Gui::ExpressionBinding` (already a
non-widget mixin) into `QuantitySpinBox` and `DoubleSpinBox`, so a
bound model owns the binding and mirrors it in two bag keys --
`binding` (the path) and `expression` (the text, or empty).  `bind`
writes `binding` once (rewriting it inside the expression-changed
handler would make the backend re-bind the real widget mid-emission,
a heap corruption the Pad gate caught); the document's own change
comes back through the binding's `onChange` into `expression` and the
value.  The Qt backend, on the `binding` key, binds the REAL
`Gui::PrefQuantitySpinBox`/`Gui::DoubleSpinBox` to the same typed path,
so its f(x) label, its read-only-while-expression state and its
expression dialog work exactly as before; `apply` is the real
widgets' `apply` (the expression stands, else the value goes to the
property through a command).  The pref widgets' history and
`selectNumber` are requests the backend forwards.  A new channel in
the core, `Fw::Backend`, carries a write straight to the one backend
(the widget shows the value BEFORE the typed signals fire, and a
panel's `QSignalBlocker` on a field, which silences the slot it hangs
on, no longer silences the mirror to the widget).

Gates: `test_expressionSeam` in `FormWidgets_Tests_run` (13/13: bind,
document -> bag -> value, the widget's dialog -> document -> bag, clear,
a `DoubleSpinBox` on a sub-path, the signal-blocker semantics) is the
in-process proof.  `SandboxNative.py` (a GUI-gate module, in the
default list) opens Pad's real panel, drives `Pad.Length` from the
widget, sets an expression on the document and reads it back read-only
through the widget, and OKs it into the `ExpressionEngine` -- RESULT
OK, 8/8 for the four-module gate.  It first ran alone: closing a
3D-view document and then pumping the event loop tripped a teardown
crash in this fork, unrelated to the sandbox (reproduced with a bare
empty document), so a run that closed the guest gates' documents and
then opened Pad's aborted.  The cause was in `ViewAreaCell`: the
hosted view is a Qt child, deleted by `~QWidget` AFTER the cell's
member destructors, and the `destroyed` lambda `hostView` connects
then stored nullptr into the already-destructed `QPointer` member --
a second weak release that freed Qt's refcount block under the dying
view, which `QObject::~QObject` then wrote into ("shared QObject was
deleted directly", a corrupted heap at the next malloc).  Fixed
2026-09-06 by deleting the view in `~ViewAreaCell` while the member is
alive; the gate with `SandboxNative` in the default list is the
regression test.

**G3b BUILT 2026-09-06**, on the C++ store: the rest of the `.ui`
subset, and every `.ui` file Draft and BIM load with `loadUi` opens
from the guest and round-trips its fields.  The class set grew by
`QDialog` with `QDialogButtonBox`, `QTabWidget`, `QStackedWidget`,
`QScrollArea`, `QSplitter`, `Gui::FileChooser`, and the item views --
`QListWidget`, `QTreeWidget`, `QTableWidget`, `QTreeView` (with
`QListView`, `QTableView`) -- on both sides (guest `items.py`, 2.9k;
the host classes, the Qt view and the store's additions, 3.3k C++).
What the build settled:

- **The item views are ROWS, and the rows are not in the bag.**  One
  representation serves the four Qt classes: a row is cells (text,
  icon, tool tip, check state, flags, colors, bold, alignment) and,
  for a tree, child rows; a view holds the top-level rows and the
  columns, the selection and the current row as state.  A thousand-row
  tree resending itself on every `setText` is exactly what the bag
  must not carry, so the rows are a tree the `Fw::ItemView` object
  owns, changed by ITEM OPS on the view's comm -- `{"item": "insert" |
  "set" | "row" | "remove" | "clear" | "sort", ...}`, one small message
  per mutation, the layout-op pattern -- that reach the backend through
  `Backend::itemsChanged` and the `itemsChanged` signal; `snapshot()`
  is the tree as data (a DOM tier's first paint, `FormWidgets.info`).
  An item built before it is attached sends nothing; `addTopLevelItem`
  sends its whole subtree as one insert.  The guest's `QStandardItemModel`
  is a plain object, not a comm: its rows cross on the comm of every
  view it is set on (`setModel` resyncs), so a DOM tier sees one kind
  of thing.  User-role data stays on the guest (it holds FreeCAD
  objects the wire could not carry); the host never needs it.
- **One Qt path through the abstract item model.**  The Qt view keeps
  a persistent index per row id and applies every op through
  `QAbstractItemModel` -- `insertRows`, `setData` by role -- whether
  the widget is a `QTreeWidget`, a `QListWidget`, a `QTableWidget`, or
  a `QTreeView` that uic left without a model (the backend makes a
  `QStandardItemModel` for it).  Where the abstract interface refuses,
  the typed call: item flags (`QTreeWidgetItem::setFlags` by walking
  the row path, a table's or a standard item created on demand) and
  header labels (a table's model has no header item to write to;
  `setHorizontalHeaderLabels` makes them).  What the user does comes
  back as state (`selection`, `currentId`, `currentColumn`) and events
  with row ids (`itemClicked`, `itemDoubleClicked`, `itemExpanded`,
  `itemEdited` -- a cell the user edited, merged into the row and
  `itemChanged` fired); the guest maps ids to its item objects and
  fires the Qt-named signals with Qt's arguments (`itemClicked(item,
  col)`, `cellChanged(r, c)`, `clicked(index)` on a view, the selection
  model's `selectionChanged`).  Header calls (`setSectionResizeMode`,
  `setStretchLastSection`, ...) are requests on the view.
- **Delegates are cell types.**  A corpus delegate dispatches
  `createEditor` on the column and returns a combo, a spin box, a
  line edit, a check box (Draft's and BIM's layer delegates).  The
  guest calls it once per column at `setItemDelegate` with a probe
  index, classifies the editor (`{"type": "combo", "items": [...]}`,
  `int`/`double` with range, `text`, `check`), closes the throwaway
  editor and sends `cellTypes`; the host's `CellDelegate` makes that
  editor and writes the text back.  The delegate's own paint,
  `setEditorData` and `setModelData` never run; the color columns'
  `QColorDialog` inside them is G3d.
- **`exec_()` is one op.**  `gui.dialog.exec [form id]` realizes the
  root as a window (`FwQt::realizeTopLevel`: a QDialog root under the
  main window, the widget dying with the model) and runs `QDialog::exec`
  inside the op; what the user does in the nested loop reaches the
  guest's slots nested (the comm messages call into the guest, as
  `addCommand`'s GetResources round trip already did), and the dialog
  code is the op's result.  `accept`/`reject`/`done`/`close`/`move`/
  `resize` are requests, `accepted`/`rejected`/`finished` events,
  `result`/`width`/`height` state.  A guest `form.show()` on a dialog
  root (`visible` TOUCHED true on an unrealized root) shows it as a
  window through the same helper -- only a dialog or a form root: a
  named child's `show()` is a property of a widget in a layout.  The
  `.ui` file's own `buttonBox -> accept` connection is uic's, so OK
  closes the dialog where the file says so and the code's
  `buttonBox.accepted.connect` where it does not.  `UiForm` IS a
  `QDialog` on both sides (a QWidget root never calls that half).
- **Containers.**  A tab widget's pages are child widgets, the titles
  the `tabs` state (translated by uic, read back at bind), the current
  one an index; `addTab`/`insertWidget`/`setWidget` cross as requests
  naming the page by model ref (`IPY_MODEL_<id>`, which the store
  resolves in any request argument), and a container built in code
  gets the pages its model collected before it had a backend.  The
  splitter's `setSizes` comes back as what Qt made of it (the ratio
  within the real width), a backend write.  `Gui::FileChooser` needs no
  file permission: the catalog offers no `fs`, and the path is data the
  HOST consumes (ShapeString's font file); its PySide wrapper is a
  plain `QWidget`, so a host test drives it by Q_PROPERTY.
- **Lessons the gate taught.**  Qt's modeless `QDialog.open()` on the
  guest class shadowed ipywidgets' `Widget.open()`, the method that
  makes the comm: no dialog form had one until the harness asked for
  `model_id` (the guest is exercised on the host's Python now:
  the wheel's modules with a stubbed bridge load all 40 files in a
  second, the loop that found it).  Qt's `close()` keeps the object
  and ipywidgets' closes the comm, so a dialog's `close` is Qt's and
  `__del__` the comm's.  A label's text flows one way (no user edit
  reports it).  `deleteLater` outside an event loop needs the deferred
  deletes flushed explicitly.  A python-mode `evaluate` keeps the
  module object it first imported, so a guest harness module is
  defined once per process and reset per test.  The QtWidgets shim
  in the guest image imports `QDialogButtonBox` from `qtdata` by name,
  so that module answers the name with the widget class through a
  module `__getattr__` (the image untouched; `QtCore.QModelIndex` is
  likewise not in the shim, and the corpus never touches it).
- **Left for later.**  A bare `QWidget` root shown with `show()` as a
  window (still left after G3c: DraftGui's tray is a tool bar the main
  window takes, and its panel goes through Control); `QDialogButtonBox.
  addButton(widget, role)`; `QDockWidget` (one BIM site); the delegates'
  color editors (G3d); a host-side sort leaves the guest's row order as
  inserted (ids still map).

Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`,
`SandboxPanels`):

    the 40 .ui files loaded in the guest, shown,   1.4 s in all, 672 objects in the
      round-tripped, closed                          store; load 2-32 ms, show 2-39 ms each
    dialogNudgeValue.ui exec_() from the guest,    0.22 s (the timer's 0.2 s inside it)
      OK clicked by a host timer inside the loop

Gate `SandboxPanels` (2 cases): every one of the 40 files behind the
corpus's 41 `loadUi` sites (one site is Draft's generic
`loadUi(ui_file)` helper) loads in the guest with every named widget
of a known class as that class's model, shows -- a QDialog root as a
window, a QWidget root as a task panel -- with every named widget
bound to uic's, and round-trips one field of each kind it has (text,
plain text, int, double, quantity, check, combo, tabs, list, tree,
table, a tree view over a `QStandardItemModel` with a nested row,
file, the button box's `clicked`/`accepted`, the splitter's sizes,
the scroll area) guest to host and host to guest; `exec_()` returns
the dialog code the host's OK produced, `accepted` having crossed.
`tests/src/Gui/FormWidgets.cpp` gained `test_itemViews` (the four
views built from native models: rows, children, checks, expansion,
selection and current both ways, a widget edit back as `itemChanged`,
clear) and `test_dialogsAndContainers` (tabs both ways, the splitter,
the scroll area, the file chooser, a dialog as a window with its
button box `exec`'d and accepted from a timer, the window dying with
the model): 15/15.  Suites after: the GUI gates 10/10 (five modules),
`Tests_run --gtest_filter='Expression*'` 104 passed + 1 skipped.

**G3c BUILT 2026-09-06**: forms built in code.  Draft's `DraftGui.py`
runs UNMODIFIED in the guest -- `DraftToolBar()` at import builds the
tray tool bar (buttons in code, the style button's icon painted with
`QPainter`, `getMainWindow().addToolBar`), `taskUi()`/`lineUi()` build
the task panel (28 items in 16 nested box layouts, a spacer, a bold
font, the field-lock actions, two event filters), and `validatePoint`
round-trips a point typed on the host.  What the build settled beyond
the sizing (7.11):

- **The layout tree crosses whole, as `layoutSpec`.**  A code-built
  layout carries a generated name (`_fcx_layout_<n>`), and every
  mutation on it re-syncs the OWNING widget's `layoutSpec` (the whole
  tree: `{class, name, items, margins, spacing}`, an item a widget ref,
  a nested layout, a spacer with its policies, a stretch, a spacing, an
  action or a separator) and then sends the op as before -- two
  messages a mutation, sixty-odd for DraftGui's panel, nothing.  The
  store rebuilds the object's `Fw::Layout` from the spec silently; the
  Qt view realizes it when it builds the widget (`View::makeLayout`,
  the real layouts named after the guest's), and the ops that follow
  find them by name.  A widget the guest hid before placing it stays
  hidden (`showInLayout`); a child no layout placed is built as a child
  still.  A `.ui` file's layouts are uic's and cross as ops only
  (`_from_ui`).
- **A tool bar's and a menu's content is a bar** (`Layout::Bar`, name
  `_fcx_bar`): widgets, actions and separators in order, realized
  without a QLayout (`fillBar`/`barOp`; a sub-menu is a `QMenu` widget
  item).  `QAction` is a model like a widget (text, icon, checkable,
  checked, enabled, visible, tool tip, shortcut), rendered by an
  `ActionView` on whatever carries it -- a widget's `addAction` (the
  `actions` state, a line edit's trailing lock icon among them), a bar,
  a menu -- or bound to a real one (the bar's `toggleViewAction`, the
  `toggleViewAction` state).  `getMainWindow()` is a shim: `addToolBar`
  realizes the bar on the host (`gui.mainwindow`, the bar dying with
  its model), `mainWindowClosed.connect` registers a hook, `showMessage`
  and `cursor().pos()` are data; `getActiveWindow`/`getWindows` wait
  for the mirror (G4).  `QMenu.exec_()` is one op (`gui.menu.exec`, a
  nested loop at the cursor), the chosen action its result and the
  menu's `triggered`; `runCommand` is `gui.cmd.run`;
  `Control.addTaskWatcher` registers each watcher as a proxy the host's
  `TaskWatcherPython` reads (`gui.control.add_watcher`).
- **The key event stream is `watchEvents`.**  A widget that installs an
  event filter or overrides `keyPressEvent` lists the QEvent types it
  wants (`watchEvents`, Qt's values; every text input asks for focus
  too, so `hasFocus()` is state), the host's relay sends each as the
  `qevent` event (`[type, key, modifiers, text, autoRepeat]`, a mouse
  event's position and buttons, a focus event's reason), the guest
  runs its filters and handler and answers `eventDone [eaten]` INSIDE
  the event, and an eaten event stops at the relay -- DraftGui's
  snap-cycling key never reaches the line edit, the lock filter's Enter
  does.  A key press costs 1.3 ms end to end.
- **`QTimer.singleShot` is a queue** the image drains at the end of
  every dispatched request (`_drain_timers`, prelude + dispatcher):
  Draft's `todo.delay` schedules `doTasks` before appending to the
  itinerary, so a shot that ran at once ran on an empty list.
- **A `QPainter` on a `QImage` records an SVG** (polygons, rectangles,
  ellipses, lines with the pen and brush) that crosses as a
  `data:image/svg+xml` icon path the host renders (`svgDataIcon`); a
  `QFont` crosses as `{bold, italic, pointSize, family}` over the
  widget's own; `sizeHint()`/`QFontMetrics` are estimates (seven
  pixels a character).  `InputField.valueChanged` carries a `Quantity`
  (the overload PySide connects; DraftGui reads `d.Value`), and
  `UiLoader().createWidget` answers None for a class outside the subset
  (DraftGui's `Gui::ToolBar` fallback).
- **The prelude delegates.**  Every `FreeCADGui` name it does not define
  is `freecad.widgets.gui.attr(name)` (the AttributeError `hasattr(Gui,
  "Snapper")` expects), and `_proxy_register` is exposed to the wheel:
  a new name is a host build, not an image rebuild.  The `fcx_draft`
  wheel gained `DraftGui`, `draftguitools/gui_field_locks` and
  `draftutils/todo`.
- **Lessons the gate taught.**  A later `def` in a class body wins over
  an earlier one (the old `installEventFilter` stub shadowed the real
  one; the host-python harness found it in a second).  Enter on a
  field locks it and, on Z, `validatePoint` unlocks every field right
  after -- natively too.  The fork's sequencer holds a WAIT CURSOR --
  whose filter eats every key and mouse event -- until a poll queued on
  the event loop sees the sequence over (`finishAggregate`), and a gate
  runs its modules in one callback: after SandboxNative's pad recompute
  the keys vanished, so a gate module spins the loop first (`settle`).
  A document principal may not open widgets (correctly refused), so a
  gate builds them through a session exec and reads through evaluate,
  where `__import__` is blocked.

Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`,
`SandboxDraftGui`):

    DraftGui imported in the guest (the tray built,    (in the host-python harness,
      the style icon painted, addToolBar)                the whole import 0.15 s)
    draftToolBar.lineUi(): the panel built in code     0.09-0.20 s, 46 objects; 352
      and shown (28 items, 16 sublayouts)                comm ops (39 opens, 253
                                                         updates, 59 ops), 19 mod_call
    a key press on the host -> the guest's filters     1.3 ms
      -> eaten or not
    a guest menu exec_() with a host click 100 ms in   0.12 s

Gate `SandboxDraftGui` (3 cases): the tray as a tool bar of the main
window with its four buttons, the painted icon, the square checkable
tool button, the toggle-view action bound (a host trigger reaching the
guest, the guest's hide reaching the bar); `lineUi()` as the active
task panel with its layout tree, the shown and hidden fields, the bold
label, the lock actions; a point typed on the host (focus, values,
Enter) delivered to the guest's callback by Enter, by the Enter Point
button and by the panel's accept, the lock filter locking on Enter and
unlocking on a double-click, the snap-cycling key eaten, a digit not;
`offsetUi()` and a close from the guest; a command registered and run
from the guest, the main window's message and close hook, a guest
menu run modally with a host click choosing its second item; a
document principal refused.  `tests/src/Gui/FormWidgets.cpp` gained
`test_codeBuiltLayouts` and `test_barsActionsAndKeys`: 17/17.  Suites
after: the six GUI gate modules 13/13, `Tests_run
--gtest_filter='Expression*'` 104 passed + 1 skipped.

The **G2b runner** followed, BUILT 2026-09-06 (7.9): Draft's and BIM's
`InitGui.py` in the guest at startup, Draft's tool bars from the guest;
both `Initialize()`s now stop at the GuiUp ruling (sec 13), then G3d.

**G3d BUILT 2026-09-07**: the selection input and the U2 dialogs.

- **`FreeCADGui.Selection` in the guest is the host's**, one op per
  call (`gui.sel.call [name, args]`, `freecad.widgets.gui.Selection`):
  the arguments cross by value with an object as its handle, and the
  host calls its own `FreeCADGui.Selection.<name>` -- no second
  implementation of the resolve modes, the sub-name grammar or the
  stack.  The subset is the 18 methods Draft and BIM reach for
  (`getSelection` 198 sites, `getSelectionEx` 66, `clearSelection` 55,
  `addSelection` 45, then `hasSelection`, `getCompleteSelection`,
  `removeSelection`, `isSelected`, `countObjectsOfType`, the
  preselection pair, the picked list); a name outside it is an
  AttributeError naming it (`addSelectionGate` is the one the linter
  still lists).  **A SelectionObject crosses by value**: `Object` and
  `Document` as handles, the names and the picked points as data, and
  `SubObjects` resolved in the guest through `Object.getSubObject` on
  first read -- `SelectionObjectPy.xml` needs no annotation, and the
  facade generator's list did not grow.
- **An observer is a guest proxy** with the eight-hook list
  (`SEL_OBSERVER_HOOKS`), and the host's own `SelectionObserverPython`
  holds the stand-in and drives it exactly as it drives a native Python
  observer: one hop per hook the class defines, the arguments by value
  (document and object NAMES, the sub-element, the point).  No new
  transport: the task watchers' route (G3c).  `removeObserver` finds
  the stand-in by proxy id; a guest reset drops every observer
  (their stand-ins point at a guest that is gone) from the boot
  listener that re-runs the InitGui's.  BIM registers ten of these,
  Draft none.
- **The four stock dialogs are one synchronous op each**, in
  `freecad.widgets.models` so both `QtWidgets` and `QtGui` (BIM's
  spelling) find them: `gui.dialog.message` for `QMessageBox`'s
  statics and its instance shape (`setText`, `setStandardButtons`,
  `exec_`; the button pressed as Qt's StandardButton value, the enum
  the `QDialogButtonBox` constants already carried), `gui.dialog.input`
  for `QInputDialog.getText/getItem/getInt/getDouble/getMultiLineText`
  (`(value, ok)` as PySide answers), `gui.dialog.file` for
  `QFileDialog.getOpenFileName/getOpenFileNames/getSaveFileName/
  getExistingDirectory` (`(path, selected filter)`), `gui.dialog.color`
  for `QColorDialog.getColor` (a QColor, invalid when canceled --
  `QColor.isValid()` is real now).  The host runs the nested loop under
  its main window; a `parent` argument is accepted and ignored.  **The
  file dialog's answer is DATA**: whether the guest may then read or
  write that path is the file-system grant's business (7.1, U2), which
  is not built -- there is no `fs` permission in the catalog yet.
- Not in G3d: the delegates' color editors inside the item views (the
  static exists, the delegate's editor path is G3b's `cellTypes`
  contract) and `addSelectionGate`.

Gate `SandboxSelection` (4 cases): the guest selects by handle and by
names, the host sees it; `getSelectionEx` with a face, `SubObjects`
resolved through `getSubObject`; the host's changes are the guest's
next read; an observer sees the host's add, add with a point, remove
and clear in order, sees the guest's own add (a nested entry), and
nothing once removed; each dialog driven from a host timer inside the
nested loop (Yes clicked, an instance's Save, text retyped, the third
item, an int canceled, a file picked, a save named, a color chosen and
a color canceled); a document principal refused.
Measured (RelWithDebInfo, Xvfb, `scripts/sandbox-gui-gate.py`,
`SandboxSelection`): the four cases in 4.1 s; a stock dialog from the
guest's call to its answer, the host's timer clicking it at 30 ms,
35-60 ms each (the first QMessageBox 0.26 s, its construction); the
observer's four host events delivered in order, the guest's own add
seen through the nested entry.  What the build settled: a session
guest holds an object handle for ONE evaluation (no owner document to
re-resolve it in), so `Selection.getSelection()` is read where it is
used, and BIM's observers get names, as natively; `FullName` is the
host's verbatim (the Python expression that re-selects); the widget
QFileDialog lists its directory asynchronously, so a driver selects by
its name edit, not `selectFile`; `getCompleteSelection`,
`getPickedList` and the preselection answer SelectionObjects, as the
host does, wrapped on the guest side by shape.

### 7.13 The session document sized: `FreeCAD.ActiveDocument` for a command **[sized and RULED 2026-09-07; S1 and S2 BUILT 2026-09-07]**

The question, asked after G3d ("next session size it first"): a guest
command can read the selection now, but its `Activated()` runs as the
session principal, and the session has no document -- so Draft's
`gui_base.Activated` (`self.doc = App.ActiveDocument`, `finish()` on
None) and BIM's command modules (490 `ActiveDocument` uses in 60 files)
stop at their first line.  What does it take for a command body to
read, hold and write the document the user is working in, which
command bodies that unblocks, under which permission, and what is the
gate.  Sized, then ruled the same day (below); the build starts
the next session.

**Why the gap exists.**  Reach on the wire is anchored on ONE object,
`HandleTable::owner()`, set by `Private::Transaction` for each
host->guest call (`ExpressionImageHost.cpp`): `evalExpression` and a
proxy op whose first argument is a document object set it (an
`execute()`, an `onChanged`); `exec` and a proxy op with no such
argument -- a command's `Activated`/`IsActive`, a workbench's hooks,
the InitGui runner, and every `gui.*` op made inside them -- pass
nullptr.  Three things hang on the owner (`ExpressionImageBridge.cpp`):
the write gate (`writeGate`: the target is the owner or in the owner's
document, then `doc.write.self`; no owner, no writes -- "writes are
allowed on the evaluation owner's document only"), `resolveByKey` (a
durable handle re-resolves into the owner's document only, under
`doc.read.self`; no owner, refused), and `active_doc` (the owner's
`Document`; None without one).  And the table is cleared after every
routed expression evaluation at depth 0 (`ExpressionEvaluator.cpp:
121`), so a session handle lives until the next expression anywhere
evaluates, then its id is stale and its key has nowhere to
re-resolve: G3d's "a session guest holds a handle for ONE evaluation".
The principal is not the problem: `Runtime::Scope(nullptr)` is
`session`, and the catalog's session column is ALLOW for
`doc.read.self`, `doc.write.self` and `doc.foreign` alike (2.2).  The
session lacks no permission; it lacks a "self" for the reach checks
to anchor on.

**What the corpus needs** (counted 2026-09-07 over
`draftguitools/gui_*.py`, 65 files; `bimcommands/*.py`, 83; and
Draft's `draftmake`/`draftfunctions`/`draftutils`, the make_* layer a
command reaches next):

    use                                 Draft cmds   BIM cmds    draftmake+
    ----------------------------------  ----------   ---------   ----------
    ActiveDocument / self.doc, refs     130 (45 f)   490 (60 f)  ~55
    .recompute                           66 (*)       94 (*)     --
    .openTransaction / .commitTransaction 19 / 19     89 / 77    3 / 3
    .getObject                           16           80          2
    .addObject / .removeObject            4 / 4       18 / 17    35 / 5
    .Objects                              8           26          1
    .findObjects / .copyObject           --            3 / --     1 / 2
    .ActiveObject / .RootObjects         --            2 / 1     --
    .save / .saveAs                      --            1 / 1     --
    FreeCAD.setActiveDocument            --            5         --
    App.activeDocument() (the call)      --           --          3, + todo.py
    Gui.doCommand (strings)              23 + commits  196       --
    Gui.ActiveDocument.ActiveView        --           26         --  (G4)

    (*) mostly inside doCommand strings: "FreeCAD.ActiveDocument.recompute()"

By file: 13 Draft commands never touch the document, 12 touch it
without the 3D view or the snapper, 40 need the view as well; BIM 23 /
40 / 20.  So the session document alone puts 52 command modules' bodies
(12 Draft, 40 BIM) within the guest's reach as far as the DOCUMENT
goes -- `Draft_Heal` (`self.doc.openTransaction`, `heal.heal(sel)`,
`commitTransaction`: direct, no view), `BIM_Trash`/`BIM_EmptyTrash`,
`BIM_Ungroup`, `BIM_Unclone`, `BIM_Reextrude`, `BIM_Glue`, the
classification, material, IFC property and layer panels -- and the 60
that also need the view wait for G4 regardless.  Two more prerequisites
stand in front of most of the rest, and both stand on this one:

- **`Gui.doCommand` is not in the guest at all** (not a `FreeCADGui`
  name the wheel answers: AttributeError).  U4 rules it "runs under
  the CALLER's principal in the caller's guest" (7.1), so
  `Draft.make_line(...)`, `Arch.makeSite()`,
  `FreeCAD.ActiveDocument.recompute()` execute IN the session guest --
  and need the session document.  Every Draft creator commits through
  it (`todo.ToDo.doTasks`: `App.activeDocument().openTransaction`,
  `Gui.doCommand(string)` per string, `commitTransaction`); BIM 196
  uses.  Sized below as S2.
- **`IsActive` is False** for every Draft creator (`bool(
  get_3d_view())`, 7.9) and for BIM's (`hasattr(getMainWindow().
  getActiveWindow(), "getSceneGraph")`; the shim's `getActiveWindow`
  is None until G4).  CORRECTED while building S1: `Command::_invoke`
  DOES consult it ("check if it really works NOW", Command.cpp) --
  `Gui.runCommand` of a command whose IsActive is False is a silent
  no-op.  Draft's `GuiCommandSimplest.IsActive` is
  `bool(App.activeDocument())`, True in the guest since S1, so
  `Draft_Heal` runs; BIM's wait for G4 or a stub view (the gate
  stubs `IsActive` for its two BIM commands).  Not this slice.

**RULED 2026-09-07 (user): "1. agree ... should also apply to
Gui.ActiveDocument.  2. ... for a workbench, I incline to grant it
access to all documents by default."**  The three rulings, and what
each settles:

1. **No new permission for the session's document writes.**  They are
   `doc.write.self` / `doc.read.self`; reach into any other open
   document is `doc.foreign`, which the catalog already ALLOWs the
   session and addons.  The same rule backs `FreeCADGui.ActiveDocument`
   and `Gui.activeDocument()`.
2. **A workbench reaches EVERY open document.**  The session and addon
   principals' reach set is all open documents, not one anchored
   document -- the catalog's stance made mechanism.  The captured
   anchor and the switch rule of the first draft are DROPPED:
   `FreeCAD.ActiveDocument` reads the host's live active document on
   every call (BimLibrary calls `setActiveDocument` and then saves),
   and a kept handle re-resolves by its own document's name wherever
   the user's focus is, as natively -- which closes G3d's "one
   evaluation" limit for session handles outright.  A DOCUMENT
   principal keeps its own document only, always.
3. **`setActiveDocument`, `newDocument`, `closeDocument` are the
   workbench's** under one new enum value, `app.write` (DENY document,
   not promptable; ALLOW session and addon); `listDocuments` and
   `getDocument` under the existing `app.query`.  **`save()` is
   declared** (the user's own path, the document as content: Ctrl+S).
   **`saveAs(path)` takes a PICKER-BLESSED path only**: a path the
   host's file dialog returned to this guest (`gui.dialog.file`) is a
   capability the guest may hand back, for the life of the guest; any
   other path is refused naming the fs slice (7.1, U2).  BimLibrary's
   one `saveAs` passes: its path comes from `getSaveFileName`.
   `mergeProject` (2 uses, a file read) waits with the fs slice.

The concern weighed and how the design answers it: **reach follows
the PRINCIPAL, never the call shape.**  The reach set is computed from
the current principal class (`Runtime` scope stack): a document
principal gets the owner's document only, even if some later op ever
lands it in a transaction with no owner; session and addon get every
open document.  Keyed on "no owner" instead, a document hook reaching
an ownerless nested call could have written the active document under
`doc.write.self` -- no such path exists today (`gui.*` and `doCommand`
are DENY for a document), and the rule keeps a later op from opening
one.  Weighed and accepted: `closeDocument` is silent data loss
natively and stays so for a workbench (an audit line; never a
document's); a workbench writing expressions into document B changes
B's hash and voids B's grants, exactly as native editing does; with
every document open to it, the sandbox's value for a third-party
workbench is the import prompts, the network, crash isolation and the
audit trail -- the standing ruling that document-derived code is the
threat (1.4).

**S1, the design (as ruled).**

- `HandleTable::owner()` stays what it is (the object an owned
  transaction may write, the identity check for value handles).  A new
  helper, `reachable(table, App::Document*)`, answers the three checks
  from the PRINCIPAL: a document principal -> the owner's document
  only (`documentOf(table.owner())`, as now); session or addon -> any
  document in `App::GetApplication().getDocuments()`.  `writeGate`
  (same-document -> reachable, then `doc.write.self`, plus
  `doc.foreign` when the target is not the owner's document),
  `resolveByKey` (a key naming a reachable document re-resolves; a
  closed document is a ReferenceError, an unreachable one a
  PermissionError naming both), and `active_doc` (a document principal:
  the owner's Document as now; session/addon: the host's LIVE
  `getActiveDocument()` as a handle under `doc.read.self`, None with
  nothing open).  Nesting needs no anchor bookkeeping: the principal
  is the scope stack's.
- Guest side (the prelude, `ImageDispatch.cpp`: a guest image
  rebuild): `App.activeDocument()` beside the `ActiveDocument`
  property; `listDocuments()`, `getDocument(name)`, `newDocument(name,
  label, hidden)`, `closeDocument(name)`, `setActiveDocument(name)`
  as module facade entries -- the guest's Application shim knows
  every document the principal may reach, not the transaction's one.
  `FreeCADGui`: `ActiveDocument` becomes a GuiDocument over the same
  live document (`Document`, `getObject(name)` -> a view provider
  handle through the view family G2b resolves, `setEdit`/`resetEdit`,
  `Modified`), `activeDocument()` the function form; `ActiveView`
  stays G4.
- Facade (`DocumentPy.xml`, `call` tier, in the write family):
  `openTransaction`, `commitTransaction`, `abortTransaction`,
  `recompute`, `copyObject`, `save`; `saveAs` under the blessed-path
  check; read: `getObjectsByLabel` (call), `ActiveObject`,
  `RootObjects` (handle), `UndoCount`/`UndoNames`/`FileName`/`Label`
  (value).  NOT declared: `undo`/`redo`/`clearUndos` (unused);
  `App.ActiveDocument.Box` (an object read as a Document attribute) is
  `Document.__getattr__`, not a member -- `getObject` is the answer.
- The blessed paths: `gui.dialog.file` records every path it returns
  in a per-guest set on the host (cleared with the guest); `saveAs`
  checks membership before the call.  The set is the seed of the fs
  slice, not a substitute for it.
- Handles across calls: a kept `self.doc`/`self.obj` re-resolves by
  key into its own document after any `clearHandles`; a kept VIEW
  PROVIDER cannot (`handleKey` is null for one); the corpus re-reads
  `.ViewObject` each time; a `[doc, name, "vo"]` key is the one-line
  extension if something does.
- Cost: about 80 lines across `ExpressionImageBridge.{h,cpp}`,
  `ExpressionImageHost.cpp` and `SandboxGui.cpp` (the blessed set),
  the `app.write` enum value with its catalog row and name, a dozen
  XML annotations, the prelude and the wheel's GuiDocument, the gate.
  Under a day with the gate.

**S2, `doCommand` in the guest** (sized here because it stands on S1
and is the next blocker of every Draft creator; built after S1 on its
own go): `Gui.doCommand(src)` compiles and execs `src` in the guest's
`__main__` dict (the console namespace natively), `Gui.addModule(name)`
imports into it; nothing runs on the host.  One host op,
`gui.docommand {sha256, len}`, does what `Command::doCommand` does
BESIDES executing -- the macro recorder line and the audit line --
under a new enum value `gui.doCommand` (the catalog row exists, "not
in the enum yet": DENY document, ALLOW session, PROMPT addon).  Draft's
commit then runs end to end in the guest: `QTimer.singleShot(0,
doTasks)` is the shim's pending queue, drained when the request that
scheduled it returns (`_drain_timers`), so the commit runs at the end
of the same proxy call -- after `Activated` returns, as natively one
loop turn later.  About 40 lines plus the enum value; gate case: a
preselected `Draft_Upgrade` (with a selection `proceed()` runs
inline, no view callback) and `BIM_Site`.

**Gate `SandboxSessionDoc`** (a GUI gate module in the rig's default
list, seven cases):

1. A guest command's `Activated`: `FreeCAD.ActiveDocument.Name` is
   the host's, `App.activeDocument()` and `Gui.ActiveDocument.Document`
   the same object (by key), None with nothing open; after the guest's
   own `setActiveDocument(other)` all three follow.
2. Writes: `openTransaction`, `addObject("Part::Feature")`, `Label`
   and `Shape`, `ViewObject.Visibility` (the view family),
   `removeObject`, `commitTransaction`, `recompute()` returns a count;
   the host's `UndoNames` carries the name and one undo restores the
   object.
3. A handle kept on the command across two activations with a routed
   expression evaluation between them (`clearHandles`) re-resolves;
   the same handle still resolves and writes after the host activates
   ANOTHER document (the ruled behaviour, native parity); after the
   host closes its document: ReferenceError.
4. Two documents: `listDocuments()` names both, `getDocument(b)` is
   writable from a command while `a` is active; `newDocument` then
   `closeDocument` round-trips (the host's document count); a
   DOCUMENT principal's `execute()` is refused every one of the five
   (`app.write` not promptable; `getDocument` of another document a
   PermissionError).
5. Regression: a DOCUMENT principal's `execute()` sees its OWN
   document as `ActiveDocument` while another is active on the host,
   and its write into the other document is refused.
6. `save()` from a command writes the document's own file (mtime
   moves); `saveAs` to a path the guest's `getSaveFileName` returned
   (the gate's timer types it) writes it; `saveAs` to a path the
   guest made up is refused.
7. Corpus: `Draft_Heal` and `BIM_Trash` then `BIM_EmptyTrash` on a
   selected object from the guest -- direct bodies, no `doCommand`,
   no view: the object healed / in the Trash group and hidden / gone,
   and the host's undo stack holding each command's transaction.

**S1 BUILT 2026-09-07.**  Gate `SandboxSessionDoc` 7/7 in about 9 s;
the full GUI gate 30/30 (26 in the default process, the 4 InitGui
cases in theirs); Expression gtests 104 + 1 skipped (three updated to
the ruling, below).  What landed, and where it departs from the design above:

- **Reach** (`reachable(table, doc)` in `ExpressionImageBridge.cpp`,
  exported as `documentReachable` for Gui): the principal class comes
  from `Runtime::currentPrincipal()`; a document principal reaches
  `documentOf(table.owner())` only; the session, an addon, and host
  code running under NO scope (a test's `eval`, the InitGui runner's
  `exec`) reach every open document.  `writeGate`, `resolveByKey` and
  `active_doc` are as designed; a key into a document no longer open
  is a ReferenceError, into an open one out of reach a PermissionError
  naming both.
- **The document set is five bridge ops, not module facade entries**:
  `app.docs`, `app.doc`, `app.new_doc`, `app.close_doc`,
  `app.set_active_doc` (`FcxWire.h`, `applicationOp`).  A module
  facade checks one permission and nothing else, and `getDocument`
  needs the reach check in C++: a document principal WITH `app.query`
  granted still lists and gets its own document only (gate case 5).
  The three writes call the host's own `FreeCAD` functions, so the
  GUI follows them (a view for a new document, a closed one's views
  gone).  `newDocument` passes keywords: the host's takes no None for
  a name it was not given.
- **`app.write`** is in the enum (`Permission::AppWrite`), DENY and
  not promptable for a document.
- **Prelude** (guest image rebuilt): `activeDocument()`,
  `listDocuments()`, `getDocument`, `newDocument`, `closeDocument`,
  `setActiveDocument` beside the `ActiveDocument` property.
- **Wheel** (`freecad/widgets/gui.py`): `GuiDocument` over one App
  document -- `Document`, `getObject(name)` through the object's
  `ViewObject` (a REAL property on this fork, commit 17300660d8, so
  `read_prop` answers it: no view-family op needed), `setEdit`,
  `resetEdit`, `getInEdit`, `activeObject`, `Modified` through one
  op `gui.doc [name, member, args]` (`SandboxGui.cpp`) under the
  same reach check; `FreeCADGui.activeDocument()` and
  `getDocument(name)`.
- **Facade**: `DocumentPy.xml` as listed (`FileName`/`Label` are
  properties: `read_prop`, no annotation).  `GroupExtensionPy.xml`
  joined the annotated XMLs (`BIM_Trash`'s `trash.addObject(obj)`),
  its membership writes in the write family.
- **Blessed paths**: `blessPath` / `pathBlessed` /
  `clearBlessedPaths` in the bridge, fed by `gui.dialog.file`, cleared
  when a guest boots (`Private::initialize`).  `saveAs` decodes its
  path from the call's wire-encoded argument list (sec 12).
- **Two host findings.**  Any command calling `doc.copyObject`
  aborted natively ("still being filled in": the import's Restoring
  bit claimed as a live load; fixed in `refreshLiveLoad`, sec 12).
  And `Command::_invoke` DOES consult `IsActive` -- the sizing above
  said it did not; corrected in place.  A consequence of S1 itself:
  Draft's `GuiCommandSimplest.IsActive` (`bool(App.activeDocument())`)
  is now True in the guest, so those commands run; BIM's stay False
  until G4.
- **Gtests updated to the ruling**: `appModuleHasNoDocumentGraph`
  (the graph exists now: a PermissionError naming `app.query` for a
  document, not an AttributeError), `guestHandlesDurableAcrossHooks`
  (a forged key into an OPEN other document is the PermissionError, a
  key into no document a ReferenceError), `writePropSameDocument`
  (an ownerless eval, no scope, writes: host code).
- **A loss to list**: the copy `Draft_Heal` makes is a guest Draft
  object now, and its `onDocumentRestored` reaches
  `getattr(vobj, "Proxy", None)` on the HOST view-provider proxy --
  `__bool__` of a host Python instance, `unsafe.getattr`, a
  PermissionError printed on the console (the object is fine).  The
  view-provider Proxy class in the guest is G4's (7.9, BuildingPart's
  losses).

**S2 BUILT 2026-09-07.**  Gate `SandboxSessionDoc` 9/9 (two cases
added); the full GUI gate 28 in the default process + the 4 InitGui
cases in theirs; Expression gtests 104 + 1 skipped.  What landed, and
where it departs from the sizing above:

- **The permission** is in the enum (`Permission::GuiDoCommand`,
  `gui.doCommand`): DENY and not promptable for a document, ALLOW
  session, PROMPT addon -- the one row `catalogDefault` does not
  answer ALLOW for an addon.  `checkPermission` logs denials and
  prompts only, so the ALLOW trail U4 asked for is a new public
  `Runtime::auditAllowed(perm, target, context)` (`auditAllowed` at
  the chokepoint surface): one line per unique (principal, sha256,
  context) per process, like every other line.
- **The op carries the source**, not `{sha256, len}` as sized: the
  macro recorder needs the text (`MacroManager::addLine`, the console
  echo under `ScriptToPyConsole`), so `gui.docommand {src, kind}` with
  kind `app` | `gui` | `module` (`import name` for `addModule`, an App
  line as `Command::addModule` records it); the host hashes it for the
  audit target and puts `kind:len` in the context.  The source is
  never executed on the host: `doCommandRecord` in `SandboxGui.cpp`
  checks, audits and records, then replies; the wheel execs.  The
  check is the op's first act, so a refusal records nothing and the
  guest runs nothing.  The op is dispatched BEFORE the `gui` check --
  its own row, not `gui`'s (an addon holds `gui` and is prompted for
  this one).
- **The wheel** (`freecad/widgets/gui.py`): `doCommand`,
  `doCommandGui`, `addModule` -- the op, then `exec` in the guest's
  own `__main__` (`sys.modules["__main__"].__dict__`, with `FreeCAD`,
  `App`, `FreeCADGui`, `Gui` bound on first use as the host's console
  has them); `addModule` once per module name per guest.  Returns
  None; a SyntaxError or the source's own exception propagates, as
  `PyRun_String` does natively.  No image rebuild for these (the
  prelude delegates unknown `FreeCADGui` names to the wheel).
- **The commit runs in the same proxy call**, as sized: `finish()`
  queues `delayCommit` / `delayAfter` on the shim's timer queue and
  the dispatcher drains it when `Activated` returns -- the audit
  trail of a `Draft_Upgrade` reads `import Draft`, the
  `_objs_ = Draft.upgrade(...)` line, then the recompute line.
- **Three facade gaps the corpus hit**, each a `call` annotation and
  a guest image rebuild: `PropertyContainer.getEditorMode` (Draft's
  `format_object`, the line after `setEditorMode` which was declared)
  and `ViewProvider.listDisplayModes` (the same function), and
  `ViewProvider.addProperty` / `removeProperty` (`_ViewProviderSite.
  setProperties` in the guest; the bridge's view family already gated
  both, only the XML lacked the tier).
- **`Gui.ActiveDocument.ActiveView`** exists in the guest now, as
  the active-object registry only: `getActiveObject(name[, resolve])`
  and `setActiveObject(name, obj[, subname])` through the same
  `gui.doc` op (`ActiveView.<member>`, the document's active view's
  `MDIViewPy`).  `Draft.autogroup` asks it at the end of EVERY
  creator's commit (`getActiveObject("NativeIFC")`, then `"Arch"`,
  then `"part"`), so without it every Draft and BIM creator's commit
  raised after making its object and left the transaction open.  Any
  other view member raises an AttributeError naming G4;
  `hasattr(view, "getSceneGraph")` stays False, so the corpus's
  3D-view tests keep answering "no view".
- **The main window shim's MDI area** answers `findChildren` with an
  empty list: the snapper's `off()` (which `Modifier.finish` calls,
  `Gui.Snapper` existing once `gui_snapper` is imported) walks the
  MDI area's children for a QuarterWidget to set a cursor on.
- **A loss to list**: the Site's view provider is built in the guest
  (`_initializeArchObject` constructs it there), and its `attach`,
  `onChanged` and `updateData` reach `vobj.Annotation`, `RootNode`
  and `SwitchNode` -- Coin nodes, G4's mirror -- so the host prints
  one AttributeError per hook call (four on a `makeSite`); the object,
  the transaction and the recompute are right.  The gate stubs
  `IsActive` for both commands (Draft's `Modifier.IsActive` and BIM's
  ask for the 3D view).
- **A gate fact**: a package's first command module imports them all
  (`draftguitools.gui_heal` brings `gui_upgrade` with it), so a later
  loader that hooks `addCommand` around the import records nothing
  unless it `importlib.reload`s the module.

### 7.14 Host commands from the guest sized: the file and code chokepoints **[sized 2026-09-07]**

The question, asked after S2 ("shall we fine grain control doCommand?
there are commands allow file access" -- then "that's the same trap as
the deny list before sandbox work right? any better way"): a guest can
now run command scripts, and some commands read and write files or run
code from a file.  Where does the gate go.  Sized here; not built.

**`doCommand` is not the hole.**  Its source runs in the guest's own
interpreter under the caller's principal (S2), and reaches the host only
through the bridge ops the caller could call directly, each under its
own permission.  Checked over the whole declared surface (the generated
facades, the module facades): no member takes a host path except
`Document.save` (the document's own file) and `saveAs` (a picker-blessed
path only, S1).  The guest's `open()` reads pyodide's empty in-memory
file system, so `importIFC.insert("/home/x/a.ifc", doc)` in a command
string fails to find the file rather than reading it.  Classifying the
string's text would be evaded with `exec`, `getattr` or string building;
the sha256 in the audit line is a trail, not a filter.  When the fs
slice lands, each file op names its path and carries its own row, and
`doCommand` still adds nothing on top.

**The hole is `gui.cmd.run`.**  The guest's `Gui.runCommand(name,
index)` runs ANY host command by name under the plain `gui` row, with
no per-command check (`runCommandByName`, `SandboxGui.cpp`).  Three
host commands act on a file with no picker, so the user never consents:
`Std_RecentMacros` runs a macro file from the recent list, the index
choosing which (`Action.cpp:2080` -> `MacroManager::run` ->
`Interpreter::runFile`: arbitrary host Python); `Std_RecentFiles` opens
a recent file (`Action.cpp:1871` -> `Gui::Application::open`);
`Std_DlgMacroExecuteDirect` runs the macro editor's content.  A document
principal reaches none of this (`gui` is DENY for it, not promptable);
the session and addons get it for free today.  Addons are the case.

**Why not a list of command names.**  A deny list is the pre-sandbox
trap: every command classified by hand, the default open, and it decays
the moment upstream or an addon adds one.  An allow list fails closed
but is the same classification inverted, and commands are not a
declared surface the way the XML members are -- there is nothing next to
a command's `activated()` that says what it reaches.  The caller is the
wrong gate either way.

**The design: gate the primitive, keyed on the scope stack.**
`Runtime::check` returns at once when the scope stack is empty (host
code outside any evaluation is trusted, 2.4), and while a guest op runs
the stack carries the guest's principal for the whole synchronous call
-- that is what makes the `gui` check in `guiOp` work today.  When a
guest runs `Std_RecentMacros`, the host runs its `activated()` with the
guest still on the stack, so a check inside the primitive it ends up
calling fires under the guest and stays silent for the user's own click.
The primitives that read or write a host file or run code from one are
few, stable and in the core; the commands are hundreds, growing, in
every module.  The chokepoints, from the callers:

    primitive                                        covers
    -----------------------------------------------  ---------------------------------------
    Gui::Application::open / importFrom / exportTo   every Std file command, drag-and-drop,
      (Application.h:77-81)                          Std_RecentFiles, Gui.open/insert/export
    App::Application::openDocument / openDocuments,  the document loaders, loadFile
      loadFile
    App::Document::saveAs / saveCopy                 the writers by path (save writes the
                                                     document's own file: allowed, S1)
    Gui::MacroManager::run (Macro.cpp:382)           Std_RecentMacros, the macro dialog
                                                     (DlgMacroExecuteImp.cpp:393), the editor
    Gui::PythonDebugger::runFile                     the debugger's run
    Base::Interpreter::runFile                       the seam under all of the above; Base
                                                     cannot see App's runtime, so a guard
                                                     callback App installs (or the checks
                                                     sit at the callers above)

Not a chokepoint: `Interpreter::runString`.  Host C++ commands compose
Python and run it through `runString` (Std_Delete, every `doCommand`),
so gating it would refuse everything; caller-chosen content arrives as
a FILE, and `runFile` is the seam.  Any command that reaches a file
hits the gate whatever its name, present or future, Std or addon, and
whatever it went through on the way (a recent macro that opens a file
hits it twice).  Nothing is classified by hand.

**Consent stays a capability, as S1 ruled.**  A picker-driven command
(`Std_Open`, `Std_Import`, `Std_Export`, `Std_SaveAs`) runs its modal
dialog INSIDE the guest's scope -- the nested loop keeps the stack --
so a naive chokepoint would refuse the very file the user just chose.
The blessed-path set answers it: the host's own `FileDialog::
getOpenFileName` / `getSaveFileName` / `getOpenFileNames` bless the
paths they return while a scope is active (today only the guest's
`gui.dialog.file` blesses), and the primitive accepts a blessed path
without a grant.  A command with no picker has no blessed path and is
refused unless granted.  The blessing is per guest and cleared at boot,
as now.

**The rows** are the two the fs slice already designs (7.1, U2) and
one new one:

    permission        document   session   addon   notes
    ----------------  --------   -------   -----   -------------------------------
    fs.read:<path>    DENY (np)  PROMPT    PROMPT  a host file read by path, at the
                                                   chokepoints; blessed paths pass
    fs.write:<path>   DENY (np)  PROMPT    PROMPT  saveAs / saveCopy / exportTo
    host.exec:<path>  DENY (np)  PROMPT    PROMPT  code run from a host file (runFile)

One audit line each, the path as the target.  The session's PROMPT is
a change from "free": the session is the user's console and the bundled
workbenches, and natively `Gui.runCommand("Std_RecentMacros")` just
runs; under the sandbox it prompts once (scope once/session/always),
naming the path.  The user's own click never prompts: no scope.  `gui`
and `gui.doCommand` stay whole-row permissions.

**Two gaps, stated.**  (1) A command that defers its work through a
timer or a posted event finishes after the guest's scope popped, and
the check sees an empty stack.  Carrying the principal across async
boundaries is not solved in general (the widget layer has the same
edge); the file-running paths that defer are few and are to be checked
at the point they are queued -- the build enumerates the
`QTimer::singleShot` / `postEvent` sites on the paths above.  (2) Host
Python reached by host code only (`Gui.open(path)` from the console,
`Part.open`) is not gated and must not be: it has no scope.

**Gate `SandboxHostFiles`** (a GUI gate module): a guest command's
`Gui.runCommand("Std_RecentMacros", 0)` with a recent macro on the host
refused, `host.exec` in the message, an audit line with the macro's
path, the macro's side effect absent; `Std_RecentFiles` refused
(`fs.read`); `Std_Open` from the guest with the gate's timer typing a
path into the host picker opens it (blessed); `Std_SaveAs` the same
writes it; a document principal refused every one, not promptable;
`Gui.open(path)` from the test itself (no scope) unchanged; a session
grant (`ExpressionSecurity.grant`) lets the recent macro run and the
audit line reads allow.

**Cost.**  About 120 lines: the three enum rows with names and catalog
cells, `Interpreter::runFile` guard (a callback in Base, installed by
App) or checks at the two Gui callers, checks at the six App/Gui entry
points above, `FileDialog` blessing under an active scope, the gate.
Under a day with the gate.  Order: the user's call; it shares its
mechanism with N3's network rows and closes the one host-execution
path a guest has, so before N2 is the natural slot.

### 7.15 The status bar and the dock widgets sized: the last wall in `Activated()` **[sized 2026-09-07; BUILT 2026-09-07]**

**Where the two `Activated()`s stop** (7.9): Draft's at
`init_draft_statusbar.show_draft_statusbar()`, BIM's at
`BimStatus.setStatusIcons(True)` and then at `BIM_Views` (the
`RestoreBimViews` default is True, so activation opens the Views
Manager dock).  Read site by site (`init_draft_statusbar.py`,
`BimStatus.py`, `bimcommands/BimViews.py`, `grid_observer.py:115`),
what they ask for, in the order a guest would hit it:

- **`getMainWindow().addStatusBarItem(widget, id=, title=, slot="Right",
  order=)`** -- and this fork's main window HAS NO SUCH METHOD.
  `MainWindowPy` exposes eight names (`getWindows` ... `hideHint`) over
  a shiboken `QMainWindow`, and `MainWindow.cpp` says so at the hint
  label ("upstream gets that from addStatusBarItem(), which this fork
  has no equivalent of").  So natively, since the upstream Draft and
  BIM syncs (`9ea92ae952`, `82a8a4478d`), every Draft activation ends
  its two 500 ms timers in an `AttributeError` traceback and neither
  workbench has its status bar widgets.  The guest did not break this;
  the host API is built first, natively, and the guest op lands on it.
- **`statusBar().findChild(QToolBar, name)`** -- the caller's own
  registered items by object name; **`mw.findChild(QToolBar,
  "Draft Snap")`** -- a HOST tool bar (the workbench's `appendToolbar`
  built it), an existence check only.
- **`Gui.UiLoader().createWidget("Gui::ToolBar")`** -- not in `CLASSES`,
  so `None` today, and `toggleViewAction()` on it is the next error.
- **`QPushButton.setMenu(menu)`** (raises today, "G3c"), **`QActionGroup`**
  (absent: `QtGui.QActionGroup(menu)` is an `AttributeError`),
  `QAction(group)` with the group as parent, `group.triggered(action)`;
  BIM's `setNudge` walks `action.parent().parent().parent()` (group,
  menu, button) back to the nudge button.
- **`Gui.Command.get(name).getAction()[0]`** -- `FreeCADGui.Command` is
  not in the guest at all (`attr()` does not name it).  Draft's snap
  widget puts five of the guest's OWN commands' actions on a bar and
  the rest into the lock button's menu; `grid_observer` writes
  `setCheckable`/`setChecked` on `Draft_ToggleGrid`'s.  A command's
  action lives on the host (`Command::getAction()`, an `ActionGroup`
  for the group commands) and is shared into whatever bar carries it.
- **`snap_widget.children()[-1]`** -- the `QToolButton` the real bar
  made for the last action, then `setFixedWidth(40)` and
  `addAction(...)` (the button's own popup) on it.
- **`QDockWidget()`** (absent) with `setWidget(form)`, an instance
  attribute `closeEvent`, `setFloating`, `setGeometry`, `x()`/`y()`/
  `height()`, `isFloating`, `toggleViewAction()`, `dockLocationChanged`;
  **`mw.addDockWidget(area, dock)`**, **`mw.tabifyDockWidget(other,
  dock)`** where `other` is a HOST dock by name, **`mw.findChild(
  QDockWidget, name)`** for its own dock (`findWidget()`) and, in
  `ifc_viewproviders`, for the host's "Model" tree.
- **`vm.tree.state() != vm.tree.State.EditingState`** -- neither the
  method nor the enum is on the guest's item views.
- **`QTimer.singleShot(2000, self.update)`** at the end of every
  `BimViews.update()`: the guest's `singleShot` ignores the delay and
  `_drain()` runs the queue UNTIL EMPTY ("a callback may queue more")
  -- so the first update would re-queue itself inside the drain and
  spin the guest forever.  Draft's 500 ms shows and hides go through
  the same shim and merely run early.  The one blocking design item.
- `mw.frameGeometry()`/`mw.rect()` (BimStatus centres the nudge
  dialog), `QIcon.fromTheme`, `QColor.fromRgbF`, `QBrush`, the tree
  item's `background`/`font` -- all present or data.

**What is NOT this item, listed as losses**: the IFC widgets
(`nativeifc.ifc_status` is not in the guest's shim package -- no
ifcopenshell -- and `BimStatus` already guards the import: no IFC
lock button, no property editor corner buttons via `findChild(
QTabWidget, "propertyTab")`); the host's "Model" dock (`tabify` with
it and `ifc_viewproviders`' lookup answer nothing: a host dock is not
the guest's); the Views tree's icons (`obj.ViewObject.Icon` is a host
handle, not a path: the rows come without icons); `BimTutorial`'s dock
(the same model, not gated).

**The design.**

- **Host, native first: a status bar registry on `MainWindow`.**
  `addStatusBarItem(QWidget*, id, title, slot, order)`,
  `removeStatusBarItem(id)`, `statusBarItem(id)`.  Two slots, "Left"
  (the message band, `insertWidget`) and "Right" (the permanent band,
  `insertPermanentWidget`); the index is the item's rank by `order`
  among the registered items of that slot, and the fork's own fixed
  widgets register through the same call with orders above the
  workbench band (progress 700, dimensions 710, the two indicators 800
  and 810, the notification area 900) so "550-699 sits left of them"
  holds without a second mechanism.  Visibility persists per id
  (`BaseApp/Preferences/MainWindow/StatusBarItems`, bool `<id>`); the
  status bar's context menu lists the titled items with check marks.
  `MainWindowPy` gains `addStatusBarItem(widget, id=, title=, slot=,
  order=)` (keyword form, as Draft calls it) and `removeStatusBarItem`.
  This alone repairs native Draft and BIM.
- **`gui.mainwindow` grows** `addStatusBarItem [model, id, title, slot,
  order]` (realize the model under the status bar as `addToolBar`
  realizes under the main window, then register), `removeStatusBarItem
  [id]`, `hasToolBar [name]` (the host bar existence check -- a bool),
  `geometry` (the frame rect as data), `addDockWidget [model, area]`,
  `tabifyDockWidget [model, name]`, `removeDockWidget [model]`.
- **Wheel, the main window shim**: `statusBar().findChild` and
  `findChild(QToolBar, name)` over the guest's own registered bars by
  object name, a host bar answered as a name-only stub (truthy, the
  corpus tests `is None`), `findChild(QDockWidget, name)` over the
  guest's own docks only.  `"Gui::ToolBar"` joins `CLASSES` as
  `QToolBar`.
- **`FreeCADGui.Command` in the guest**: `Command.get(name)` is a
  handle by name (no op); `getInfo`, `isActive`, `run` cross (`run` is
  `gui.cmd.run`); `getAction()` answers a list of `QAction` models
  carrying a new synced `command` trait (`name`, `index`) that the host
  realizes by BINDING to the real action (`bindAction(model,
  cmd->getAction()->action())`, the group's members by index), so
  `bar.addAction(it)` puts the command's own action on the guest's bar
  and `setChecked` on it lands on the real one.  The rule: a guest may
  bind any command's action it may run (the `gui` permission, as
  `runCommand`), and may WRITE to it -- text, icon, checked, enabled,
  visible -- only when the command is its own (`guestCommands()`);
  a write to another's is dropped with a report-view line.  Draft's
  snap commands are all the guest's.
- **Models**: `QPushButton.setMenu` / `QToolButton.setMenu` as a synced
  `menu` ref the host sets on the real button; `QActionGroup` guest-
  only (members by parent or `addAction`, `exclusive`, `checkedAction`,
  `triggered(action)` aggregated from the members'); a `QToolBar`'s
  `widgetForAction(action)` and `children()` answer a lazily made
  `QToolButton` model the host binds to `tb->widgetForAction(real)`,
  with `setFixedWidth` and a bar of its own for `addAction`
  (`QToolButton::addAction`, the popup) and `setDefaultAction`; the
  item views get `state()` (`NoState`) and the `State` enum; a
  `QDockWidget` model (`widget`, `windowTitle`, `floating`, `geometry`
  and `area` written back by the host, `visible`; events `close` --
  dispatched to `self.closeEvent(ev)` so BimViews' instance attribute
  is what runs -- `dockLocationChanged`, `visibilityChanged`;
  `toggleViewAction`).  On the host a `Fw::QDockWidget` whose Qt view
  goes through `DockWindowManager::addDockWindow(name, content, area)`,
  so a guest dock is a first-class panel: in the Panels menu, in the
  saved layout, overlay-capable.
- **Timers become the host's when they have a delay.**  `QTimer.
  singleShot(msec, cb)` with `msec <= 0` stays the drain queue (todo's
  order contract: a 0 queued during a drain runs in that drain);
  `msec > 0`, and `QTimer.start()`, register a host timer (`gui.timer
  [start, id, msec, repeat]` / `[stop, id]`) that fires ONE hook into
  the guest (`fire(id)` on a per-boot dispatcher proxy, then the
  drain), keyed by guest so a reset drops them.  Where the op is
  refused (a document principal: `gui` DENY) the shim falls back to
  the queue but defers a delayed callback to the NEXT drain, so a
  self-re-arming update cannot spin.  Cost: one proxy call per firing;
  BimViews' poll is one every 2 s while the dock is visible, the same
  as native.

**Gate** -- three cases in `SandboxInitGui` (the process whose startup
ran the runner, `FCX_INITGUI_IN_GUEST=1`), plus one native:
`test_status_bar_registry` (host Python: two items ordered by `order`,
a hidden id persisted and restored, the context menu lists the titles;
this is the fork repair); `test_draft_status_bar` (after activation and
the timers, the HOST status bar holds `draft_scale_widget` and
`draft_snap_widget`, the snap bar's first action IS
`Gui.Command.get("Draft_ToggleGrid").getAction()[0]`, the lock button's
menu carries the other snap commands, triggering the group's "1:50"
action from the host sets `DefaultAnnoScaleMultiplier` and the label;
deactivation hides both); `test_bim_status_bar_and_views` (the
`BIMStatusWidget` with its two actions and the nudge button's menu,
the "BIM Views Manager" dock registered with the dock manager and
shown, its tree listing a Building and two levels after one host
timer tick, the status bar's views button toggling it, deactivation
hiding it and storing `RestoreBimViews`); `test_guest_timer` in
`SandboxWidgets` (a session guest's `singleShot(50, cb)` fires with no
traffic, a repeating timer stops on `stop()`, a document guest's
delayed callback waits for the next drain).

Budget: about 600 lines of host C++ (the registry 200, the dock model
and view 200, the command binding and the button bar 120, timers 80),
350 in the wheel and shims, 250 of gate.  Order settled 2026-09-07:
this, then G4, then F1 (sec 11).

**BUILT 2026-09-07.**  Gate `SandboxInitGui` 7/7 (its own process,
`FCX_INITGUI_IN_GUEST=1`; about 13 s), `SandboxWidgets` 3/3 with
`test_guest_timer`, the full default GUI gate list 36/36, `Tests_run
--gtest_filter='Expression*'` 104 passed + 1 skipped.  Both
`Activated()`s now run to their last line from the guest; what was
built, and what the run taught:

- **The host registry** -- SUPERSEDED 2026-09-10 by the RemoteEdit
  merge (sec 11): LinkVibe had built the same registry (`c324d79dfd`,
  `StatusBarItemSpec`, `MainWindow/StatusBar`), and the merge keeps
  that one, with our `statusBarItem` / `isStatusBarItem` on top; the
  note below records what this branch had built.  (`MainWindow::
  addStatusBarItem` / `removeStatusBarItem` / `statusBarItem` /
  `statusBarItems` / `isStatusBarItem`, `MainWindowPy` the same by
  keyword) as sized: the
  fork's seven fixtures register with orders 100-200 left and 700-900
  right, the bar is re-added in order on every registration (QStatusBar's
  insert indices are absolute and it hides what it removes, so each
  widget's visibility is kept across the re-add), a titled item's
  visibility persists under `MainWindow/StatusBarItems`, the bar's
  context menu lists the titled items and defers to the tool bar
  manager's own menu over its `StatusBarArea`.
- **Two fork bugs under the sandbox one, both native.**  `UiLoader().
  createWidget("Gui::ToolBar")` made nothing (the widget factory has
  no such class; upstream's is a QToolBar the manager decorates): the
  loader answers a `QToolBar` for that name now.  And the
  `ToolBarManager` treats EVERY `QToolBar` parented to the status bar
  as one of its own -- adopts it into its `StatusBarArea`, hides it on
  a workbench switch, records it in its saved state and pre-creates an
  empty, untitled bar of that name at the next startup.  That is
  Draft's own comment ("the toolbar sometimes jumps out of the status
  bar to any other dock area"), and it is why the gate's second run
  lost widgets the first had shown: `ToolBarManager::toolBars()` skips
  a registered status bar item now.  A stale saved state still
  pre-creates the untitled bars, so `hasToolBar` answers only a bar
  WITH a title.
- **`FreeCADGui.Command` in the guest** (`freecad/widgets/gui.py`):
  `get` is one `gui.cmd.info` op (null for no such command), `getInfo`
  / `isActive` the same op, `run` is `gui.cmd.run`, `getAction()` the
  `QAction` models with the `command` / `commandIndex` traits the host
  binds (`FwQt::realizeAction` -> `commandAction`: `Command::
  getAction()`, `initAction()` first when the command sits in no bar,
  the group's member by index).  The write rule is
  `FwQt::setCommandWriteFilter`, set by `SandboxGui` to `guestCommands()`
  membership; a refused write is one report-view line per model.
- **Models**: `QAbstractButton.menu` (a `QPushButton`/`QToolButton`
  gets the realized `QMenu`; a tool button's popup mode becomes
  InstantPopup), `QToolButton.forAction` / `defaultAction`,
  `QAction.command`, `QDockWidget` (`Fw::QDockWidget`, class table
  entries `QDockWidget` and `Gui::ToolBar`), `QActionGroup` guest-only,
  the item views' `state()` / `State`, `Qt.Orientation` and
  `Qt.DockWidgetArea` scoped enums, `QPoint` arithmetic (BIM_Welcome
  centres itself with it).  **`widgetForAction` needed one more
  thing**: a constructor parent does not cross (only `setParent` does,
  once the comm exists), so the button model sends `setParent` itself
  and the host binds it on comm open, update AND custom events;
  failing a known parent, the bar is the one that carries the real
  action (`QAction::associatedObjects`).
- **The dock** goes through `DockWindowManager::addDockWindow` on the
  CONTENT widget (the manager makes the `QDockWidget`: Panels menu,
  saved layout, overlay title bar) and `FwQt::View::bind` binds the
  dock model to it; `DockRelay` reports Close (the guest's
  `closeEvent`, an instance attribute in BimViews) and writes the
  geometry back; `visibilityChanged` / `dockLocationChanged` /
  `topLevelChanged` cross as state and events.  The BIM Views Manager
  lists a Building and its two levels from the guest, the status bar's
  views button toggles it through the guest's `BIM_Views`, deactivation
  hides it and stores `RestoreBimViews`.
- **Timers**: `gui.timer` as sized (`dispatcher` / `start` / `stop`,
  `QTimer::deleteLater` throughout -- a repeating timer stops itself
  from inside its own timeout), dropped with the selection observers
  on a boot; the shim defers a refused delayed shot to the next drain.
- **Two more gaps BIM's `Activated()` hit past the status bar**, both
  filled: `BimSelect` joined the BIM wheel (its observer waits on
  `FreeCADGui.addDocumentObserver`, absent, so it stands down), and
  `Gui.addWorkbenchManipulator` / `removeWorkbenchManipulator` cross
  as `gui.wb.manipulator` -- the guest object is a stand-in with the
  four `modify*` hooks, the host's own Python manipulator wrapper calls
  them by name; `Gui.activeWorkbench()` inside a guest workbench's
  `Deactivated()` is the HOST's next workbench (the switch has
  happened), answered as a `_HostWorkbench` with `name()` and
  `reloadActive()` (`gui.wb` takes those two on a host workbench).
  `FreeCAD.isRestoring()` is in the prelude (False; guest image
  rebuilt).
- **Gate facts**: BIM's first activation runs `BIM_Welcome`, a modal
  the guest exec's -- a hang under Xvfb, as natively on a first run, so
  `SandboxInitGui.setUp` sets `Mod/BIM FirstTime` off; the gate homes
  (`/tmp/fchome*`) carried the manager's adopted-bar state between runs
  and had to be wiped once; a status bar item's order is asserted by
  `x()`, `QStatusBar::layout()->indexOf` answers -1 for its items.
- **Losses, as listed above**: the IFC widgets, the property editor
  corner buttons, the host's "Model" dock, the Views tree's icons; and
  the BuildingPart view providers' `onChanged` (G4) print through
  BimViews' updates.

### 7.16 G4 sized: the mirror -- the guest's Coin scene on the host's screen **[sized 2026-09-07]**

**Where the guest stops now.**  Every `Activated()` runs to its end
(7.15), the session document is a command's (7.13), the selection and
the stock dialogs cross (G3d).  What is left of a Draft or BIM command
is the 3D view: `get_3d_view()` answers None because
`getMainWindow().getActiveWindow()` answers None in the guest, so every
Draft Creator's `IsActive()` is False and BIM's `hasattr(getActiveWindow(),
"getSceneGraph")` polls (`InitGui.py` x11, `BimArchUtils.py` x6) keep
its commands silent; a guest-built view provider's `attach` reaches
`RootNode`/`Annotation`/`SwitchNode` and prints an AttributeError per
hook (7.13, the S2 built note); the trackers and the snapper build real
Coin graphs in the guest (7.10: the pivy wheel, `pivy.coin` 0.55 s to
import) that nothing displays.  The decision of 7.2 stands: Coin in the
guest, the HOST scene holding a replica through a mirror reader, and a
node-type allowlist at the reader.  What 7.2 left open -- "generated
Coin models, the model classes from Coin's field introspection" -- is
moot since Probe A: the guest holds real `SoNode`s, so the wire is a
WALK of real nodes, not a model library.

Read site by site (the corpus: 600 `coin.` uses in 38 files, 49 Coin
classes; the view members; the view provider hooks), what a guest asks
for:

- **Nodes.**  `SoSeparator` 112, `SoCoordinate3` 47, `SoTransform` 46,
  `SoMaterial`/`SoDrawStyle` 33 each, `SoAsciiText` 32, `SoText2` 21,
  `SoLineSet` 19, `SoSwitch` 15, `SoIndexedFaceSet` 13, `SoBaseColor` 12,
  `SoGroup`/`SoFont` 11, `SoTexture2` 7 (Draft's hatch and BIM's
  covering textures: the `image` field from an `SoSFImage` built in
  the guest, never `filename`), `SoTextureCoordinatePlane` 6,
  `SoMarkerSet`/`SoAnnotation` 5, `SoSphere`/`SoShapeHints`/`SoPickStyle`
  4, `SoTexture2Transform`/`SoIndexedLineSet`/`SoCube`/`SoClipPlane` 3,
  and one or two each of `SoVertexProperty`, `SoMaterialBinding`,
  `SoFaceSet`, `SoCone`, `SoPointSet`, `SoMatrixTransform`,
  `SoLightModel`, `SoDirectionalLight`, the cameras.  No engines, no
  field connections (`connectFrom` 0), no `SoCallback`, no
  `SoEventCallback` node in a guest graph.  The ghost trackers'
  `writeInventor -> SoInput -> SoDB.readAll` (10 sites) is guest-local
  and already works.
- **FreeCAD's own node types, created by name** (`SoType.fromName(...).
  createInstance()`): `SoBrepEdgeSet` x9, `SoFCSelection`, `SoDatumLabel`,
  `SoSkipBoundingGroup`, `SoBrepPointSet`, `SoBrepFaceSet` -- one each.
  Draft then writes their fields by name (`coordIndex`, `documentName`,
  `objectName`, `pnts`, `lineWidth`, ...): pivy's dynamic field access
  (`getField` behind `__getattr__`) needs no SWIG class, only a
  REGISTERED SoType with the fields.  In the guest today
  `fromName("SoFCSelection")` is a bad type: the six are stand-ins to
  build (below).
- **The host's roots a guest node is put under.**  A tracker's switch
  goes into the active view's AUXILIARY graph (`getAuxSceneGraph()`,
  this fork's own root for temporary geometry, `get_scene_graph()` in
  `gui_trackers.py`) with `addChild`/`insertChild(node, 0)`/
  `removeChild`/`findChild`; a view provider's `attach` calls
  `vobj.addDisplayMode(node, name)` (26 Draft, 41 BIM display-mode
  uses) and `vobj.Annotation.addChild` (ArchSite's compass), reads
  `vobj.RootNode.getNumChildren()`, writes `vobj.SwitchNode.defaultChild`
  (ArchComponent); BuildingPart's and SectionPlane's `CutView` insert an
  `SoClipPlane` at index 0 of the VIEW's scene root from the view
  provider's `onChanged`.
- **The view** (`Gui.ActiveDocument.ActiveView`, `getActiveWindow()`):
  `getSceneGraph`/`getAuxSceneGraph`, `getCameraNode` (11 sites:
  WorkingPlane reads `position`/`orientation`/`height`/`heightAngle`
  and tests `isinstance(n, coin.SoOrthographicCamera)`),
  `getViewDirection`, `getCameraOrientation`, `viewTop`/`viewIsometric`,
  `fitAll`, `redraw`, `getPoint` (50 sites), `getPointOnScreen`,
  `getCursorPos`, `getObjectInfo`/`getObjectsInfo` (13: the snapper's
  objects under the cursor -- dicts of `Document`/`Object`/`Component`
  /`x`/`y`/`z`), `getViewer().getSoRenderManager().getViewportRegion()`
  (3: the ghost tracker's `SoGetMatrixAction`, `gui_edit`'s
  `SoRayPickAction` over the HOST scene with a radius, searching its
  own edit-tracker node in the picked paths), `setAnimationEnabled`,
  `getName`/`setName`.
- **The event stream.**  `addEventCallback("SoEvent", cb)` 43 sites,
  the callback reading `arg["Type"]` (68), `Position` (29), `State` (26),
  `Key` (24), `Button` (23), `ShiftDown`/`CtrlDown` (2 each) -- the
  dict the host already builds in `View3DInventorPy::eventCallback`;
  `addEventCallbackPivy(SoMouseButtonEvent/SoLocation2Event.
  getClassTypeId(), cb)` 6 sites (the snapper's click and move,
  `gui_edit`), the callback taking an `SoEventCallback` and reading
  `getEvent()` then `getPosition`/`getState`/`getButton`/`getKey`/
  `wasCtrlDown`/`wasShiftDown`/`wasAltDown`, and `setHandled()` once
  (the snapper's click, to stop navigation).  Nothing reads
  `getAction()`, `getPickedPoint()` or `getPath()` off the callback.
- **The view observer.**  WorkingPlane and `grid_observer` connect
  `mw.findChild(QMdiArea).subWindowActivated` (4 sites) to re-read the
  camera when the active view changes.
- **View providers in the guest.**  Draft and BIM define `onChanged`
  75, `getIcon` 66, `attach` 47, `setEdit` 35, `updateData` 34,
  `setupContextMenu` 29, `unsetEdit` 25, `claimChildren` 21,
  `getDisplayModes` 16, `setDisplayMode` 15, `doubleClicked` 14,
  `getDefaultDisplayMode` 11, `onBeforeChange` 9, `onDelete` 8,
  `isShow` 3, the six drag/drop hooks 2 each, `dumps`/`loads`.  The
  host's table (`ViewProviderFeaturePython.h`) has 47 hooks; the ones
  that take or return Coin objects (`getElementPicked`, `getElement`,
  `getDetail`, `getDetailPath`, `getSelectionShape`, `setEditViewer`,
  `unsetEditViewer`, `canAddToSceneGraph`) are defined by NO Draft or
  BIM view provider, so they never cross and the C++ default runs.
  Today a view provider's Proxy restores NATIVELY under routing
  (`PropertyPythonObject::restoreObject`: "no document object as
  container"), the last host import of a document-chosen module name
  (sec 13, "still open"); a view provider BUILT from the guest
  (`Arch.makeSite()` under S2) already holds a guest stand-in whose
  hooks cross -- they fail on the nodes.
- **What is NOT this item**, listed as losses: `FreeCADGui.createViewer()`
  and everything offline (`ArchSectionPlane`'s image render,
  `OfflineRenderingUtils`: `SoOffscreenRenderer`, Quarter,
  `SoWriteAction` to a file -- GL and files, neither in the guest);
  `SoTexture2.filename` (dropped by the reader: a host path); field
  connections and engines (not mirrored: a report line); `SoShadowGroup`
  (commented out in the corpus already); a `CutView` clip plane inserted
  in the VIEW root from a DOCUMENT principal (below).

**The design.**

- **The wire is a walk, one op per guest turn.**  The guest keeps, per
  host root it was handed, the list of guest nodes attached there; a
  mirrored node has an id and the `getNodeId()` it was last sent with.
  Coin bumps a node's unique id on every field write and, through the
  child-list auditors, every ancestor's (`SoNode::notify`,
  `SET_UNIQUE_NODE_ID`), so the walk from an attached root prunes any
  subtree whose root id is unchanged: a quiet scene costs one integer
  compare per attached root, a moved tracker costs its own path.  The
  records: `new [id, type, fields]`, `set [id, fields]`, `children
  [id, [ids]]`, `del [ids]` (a node no longer reachable from any
  attached root).  Fields are TYPED on the wire -- a switch over Coin's
  field types on both sides (the SF/MF bool, int, uint, short, float,
  double, string, name, enum and bitmask AS NAMES, vec2/3/4 f and d,
  color, rotation, matrix, plane, time; `SoSFImage` as `[w, h, nc,
  bytes]`; `SoSFNode`/`SoMFNode`/`SoSFPath` as mirror ids) -- never
  Coin's Inventor parser on host-side text, and an `SoSFNode` set by
  text would BE that parser (7.2's rule, kept).  The flush is the last
  op of the guest's turn, inside the hop that called it: `_drain`
  (the todo queue) and every hook return call `scene.flush()`, so a
  view provider's `attach` returns to the host with its nodes already
  in place, and a mouse move that moved a tracker costs one callback
  hop plus one `gui.scene.sync`.  Guest side: `freecad/widgets/scene.py`
  (the wheel; pure Python over pivy).
- **The reader** (`src/Gui/SandboxScene.cpp`, new; one `SceneMirror`
  per guest, dropped on a boot): id -> ref'd `SoNode*`, a node made by
  `SoType::fromName(type).createInstance()` only if `type` is in the
  ALLOWLIST -- the Coin types the corpus uses (above) plus the common
  shape, property, transform, group and light nodes, and the six fork
  types -- so `SoFile`, `SoImage`, `SoWWWInline`, `SoWWWAnchor`, the
  shader nodes, `SoCallback`, `SoEventCallback`, `SoJavaScriptEngine`,
  the VRML script, inline and texture-by-URL nodes, and any engine
  never exist on the host; `SoTexture2.filename` and every other
  path- or URL-carrying field is refused by name.  Quotas per guest:
  200 000 nodes, 64 MB of image bytes, 16 MB per sync, 64 KB per
  string; a sync past a quota is refused whole with one report-view
  line and the guest's scene stays as it is.  An `SoFCSelection`
  whose `documentName` the principal cannot reach (`reachable`, 7.13)
  is refused the same way.
- **The fork's node types are REAL SoTypes in the guest**: a small C++
  unit compiled into `_coin.so` beside pivy (`src/App/PyodideHost/pivy/`,
  the wheel rebuilt outside conda), `SO_NODE_SOURCE` subclasses of
  `SoIndexedLineSet` / `SoIndexedFaceSet` / `SoPointSet` / `SoSeparator`
  / `SoShape` / `SoGroup` carrying the same fields as the host classes
  (`SoBrepEdgeSet`: `highlightIndices`, `highlightColor`, `seamIndices`,
  `elementSelectable`, `onTopPattern`, `attachedOnly`; `SoFCSelection`:
  `documentName`, `objectName`, `subElementName`, `style`,
  `selectionMode`, `highlightMode`, `selected`, `useNewSelection`, the
  two colors; `SoDatumLabel`'s thirteen; ...) and no traversal
  behaviour -- the guest never renders.  `fromName` and the dynamic
  field access then work as they do natively, the walker sends the
  type NAME, and the host creates the real class.
- **Host roots as guest proxies** (`_HostNode` in the wheel): the
  active view's `getSceneGraph()` and `getAuxSceneGraph()`, and a view
  provider's `RootNode`/`Annotation`/`SwitchNode` (the handle plus
  which).  They take `addChild`/`insertChild`/`removeChild`/
  `replaceChild`/`findChild`/`getNumChildren`/`getChild` over the
  guest's OWN attached children (`gui.scene.attach [root, id, index]`
  / `detach`, the subtree riding the same op); a host-native child is
  an opaque `_HostNode` with `getTypeId().getName()` and `getName()`
  only; the one field write the corpus makes on a host node
  (`SwitchNode.defaultChild`, `whichChild`) is a named short list.
  `vobj.addDisplayMode(node, name)` mirrors the subtree and calls
  `ViewProvider::addDisplayMaskMode` on the host.  **Reach**: a session
  or addon guest reaches the active view's two roots and every view
  provider it may write (S1's reach) under `gui`; a DOCUMENT principal
  reaches only the three roots of its OWN view providers (the view
  family, `doc.write.self`) and never the view's root -- so a routed
  document's `CutView` (BuildingPart, SectionPlane) from `onChanged`
  is refused with a report line, the same standing as `gui` DENY for a
  document: a file must not clip the user's whole view by itself.  No
  new permission.
- **The camera is a snapshot node, written back at the drain.**
  `getCameraNode()` answers a REAL guest `SoOrthographicCamera` or
  `SoPerspectiveCamera` (so `isinstance` holds) filled from the host's
  fields at the call (`gui.view [get_camera]`); a guest write to it is
  seen by the next flush (its node id moved) and sent back as
  `[set_camera, fields]`.  `getCameraOrientation`, `getViewDirection`,
  `setCamera*`, the `view*` orientations, `fitAll`, `redraw`, `getPoint`,
  `getPointOnScreen`/`OnViewport`/`OnFocalPlane`, `getCursorPos`,
  `getSize`, `getObjectInfo`/`getObjectsInfo` (each hit filtered by
  the principal's reach: a foreign document's object is dropped from
  the list), `setAnimationEnabled`, `getName`/`setName`,
  `hasClippingPlane`/`toggleClippingPlane`: one op family `gui.view
  [view, member, args]`, all data, under `gui`.  `getViewer()` is a
  shim whose `getSoRenderManager().getViewportRegion()` builds a guest
  `SbViewportRegion` from the host's size, and whose `getSceneGraph()`
  is the host root proxy; `gui_edit`'s `SoRayPickAction` over it
  becomes `gui.view [pick, x, y, radius, all]` answering the picked
  points as `(point, normal, path)` with the path's nodes as the
  guest's own node where the id is the guest's and opaque `_HostNode`s
  elsewhere -- `searchEditNode`'s `path.getNode(length - 2)` finds its
  tracker.  The `SoRayPickAction` class in the guest stays real
  (guest-local picking over guest graphs works); applied to a
  `_HostNode` it is the op.
- **Events cross as data, one hop each.**  `addEventCallback(type, cb)`
  registers a native callback on the viewer (`gui.view.event [add,
  type, proxy]`) that calls the guest proxy with the same dict the host
  builds today; `addEventCallbackPivy(SoType, cb)` registers an
  `SoEventCallback` hook that sends `[type, position, button, state,
  key, printable, ctrl, shift, alt, time]`; the guest's `_EventCallback`
  shim builds a REAL guest `SoMouseButtonEvent` / `SoLocation2Event` /
  `SoKeyboardEvent` from it (pivy has the setters), `getEvent()` answers
  it, and `setHandled()` sets a flag the reply carries, so the host
  calls `n->setHandled()` after the hop and navigation stops as
  natively.  `getAction`/`getPickedPoint`/`getPath` off the callback
  raise (unused).  A mouse move is one hop (the wire floor of 8.1, about
  22 us, plus the snapper's own Python) and, when a tracker
  moved, one sync.  Callbacks are keyed by guest and dropped on a
  boot, as the selection observers and timers are.
- **The active window and the MDI observer.**  `getActiveWindow()`
  answers a `_View3D` proxy (a per-guest table on the host, resolved
  by document name and view serial; a closed view's proxy is a
  `ReferenceError`) whenever the host's active window is a 3D view --
  which makes `get_3d_view()` true, Draft's Creators `IsActive`, and
  BIM's `hasattr(..., "getSceneGraph")` polls pass with no op (the
  proxy has the attribute).  `getWindows`/`getWindowsOfType` list the
  3D views; `setActiveWindow` crosses.  `mw.findChild(QMdiArea)` is a
  shim whose `subWindowActivated` is driven by
  `Application::signalActivateView` through `gui.view.observer`, one
  hop per switch.
- **View providers in the guest.**  `PropertyPythonObject::
  restoreObject` routes a `Gui::ViewProvider` container's Proxy through
  `restoreGuestProxy` keyed on the view provider's Object (the reach
  anchor), closing sec 13's last document-chosen host import; the 47
  hook names join `isHookName` and the prelude's HOOKS so the host's
  probes cost no trip; the arguments already cross (`vobj` and `obj` as
  handles, `prop`/`mode`/`subname` as data); `setupContextMenu(vobj,
  menu)` gets a guest `QMenu` model (G3c) whose actions the host
  appends to the real menu after the hop, `getIcon` returns a path or
  XPM text (data), `claimChildren` handles, `setEdit` opens the G3
  panels.  The GuiUp losses of sec 13 close here: BuildingPart's
  `ViewObject.Proxy.onChanged` from `execute` and Layer's
  `change_view_properties` are guest-to-guest calls once the view
  provider's Proxy lives in the same guest.

**Order, three stages, each shipping alone:**

- **G4a -- the mirror and the drawing loop**: the walker, the reader,
  the aux and scene roots, the fork types in the guest, the view op
  family with the camera snapshot, the events, the active window and
  the MDI observer.  What it unblocks: `Draft_Line` from the guest end
  to end -- the snapper's `getPoint`, the line tracker following the
  cursor, two clicks making a Wire -- and every Draft Creator's
  `IsActive`, every BIM command's poll.
- **G4b -- view providers in the guest**: the restore route, the hook
  table, `addDisplayMode` and the three roots, the context menu.  What
  it unblocks: a routed document's Draft and BIM objects DRAWN from the
  guest (`SandboxCorpusGui` strict for BIM too: the 3 BuildingParts
  valid), `Arch.makeSite()`'s terrain from the guest.
- **G4c -- the residue**: the pick op for `Draft_Edit`, the field-write
  list on host nodes, whatever the corpus commands hit next, measured
  after G4a and G4b under the gate.

**Gate** -- `SandboxScene` (new, in the rig's default list) plus one
stage of `SandboxCorpusGui`:
`test_mirror_roundtrip` (a session guest builds a graph of every
allowed type with every field type and attaches it to the aux root;
the HOST's `SoWriteAction` text of the mirrored subtree equals the
GUEST's `SoWriteAction` text of the same subtree -- the fork types
print the same names on both sides -- then one `whichChild` and one
coordinate change cross in one sync and the texts match again, and
`detach` brings the host's node count back to zero);
`test_reader_refuses` (`SoTexture2.filename`, an `SoFile`, an
`SoCallback`, a sync past the node quota, an `SoFCSelection` naming a
foreign document: each refused with a report line, the rest applied);
`test_draft_line_from_guest` (Draft active from the guest, `Draft_Line`,
synthetic mouse moves and clicks posted to the view's GL widget under
Xvfb: the mirrored line tracker's `SoCoordinate3` on the HOST follows
the cursor, two clicks make a `Draft Wire` with the snapped points,
`setHandled` kept the view from orbiting); `test_camera_and_observer`
(WorkingPlane's `align_to_view` from the guest reads the camera, a
`viewTop` from the host fires the guest's `subWindowActivated` path
once, a guest camera write lands on the host); `test_events`
(`addEventCallback("SoEvent")` sees a key and a button dict,
`addEventCallbackPivy` gets a real guest event with the right
position and modifiers).  In `SandboxCorpusGui`, `test_view_providers_
routed`: the Draft and BIM corpus documents reopened with the view
providers in the guest, each object's `RootNode` written by
`SoWriteAction` equal to the native run's text (names normalized),
BIM STRICT.  The roadmap's pixel compare (`saveImage` of both runs)
is a secondary, off-by-default check: Mesa under Xvfb is slow and the
scene text is exact.

Budget: about 1400 lines of host C++ (the reader and the field switch
500, the roots and the view op family 400, the events and the pick
200, the view provider route and hook table 150, the camera and the
observer 150); 250 of C++ in the guest's `_coin.so` (the six types; a
pivy wheel rebuild); 900 in the wheel (the walker 300, the host node,
camera, event and view shims 400, the view provider glue and the MDI
shim 200); 600 of gate.  G4a is the larger half.

### 7.17 The document program sized: expression-language libraries in the document's guest **[sized 2026-09-10; RE-SIZED 2026-09-13 against the proxy chain, probed; D1 BUILT 2026-09-13; D2's library half and the linked library BUILT 2026-09-13; P3 BUILT 2026-09-13; D3 BUILT 2026-09-14; D4 BUILT 2026-09-14]**

Sec 11 item 2, the re-aim's one target (1.2): a program a DOCUMENT
carries, written in the expression engine's language, generating
shapes through the curated geometry surface, evaluated in the
document's guest.  Five parts were named -- (a) an example set, (b)
the surface audit and its versioning, (c) the carrier, (d)
per-document guests, (e) the gate.  Sized against the code and a
probe run on 2026-09-10 (`FreeCADCmd`, the pyodide runtime, this
box), which decided more than the reading did.

**What the language is.**  `ExpressionParser.y` is a statement
language already: `if`/`elif`/`else`, `while`, `for ... in`, `try`,
`def`, `lambda`, `return`, `import` and `from ... import`,
assignment and the augmented assignments, comprehensions, lists,
tuples, dicts, the units and the engine builtins (`vector`,
`placement`, `rotation`, `matrix`, `sin`, `sqrt`, `range`, `len`,
`str`, ...).  No classes.  `#@pybegin` / `#@pyend` is a lexer state
(python-mode strings and builtins visibility, `PseudoStatement`),
not host Python.  A property expression evaluates with a call frame
(`PropertyExpressionEngine.cpp:851`, `OptionCallFrame`), so a
multi-line program with assignments and `def` binds to a property as
it stands, and `DlgExpressionInput` edits it multi-line
(`Gui::ExpressionTextEdit`).  A shape that comes back lands in a
`Part::Feature`'s `Shape` through `Property::setPathValue` ->
`ObjectIdentifier::setValue` -> `setPyObject`, the handle decoded to
the host `TopoShapePy` before the handles clear (3.2).  So the
"program object" needs no new class: a `Part::Feature` with its
parameters as properties and the program as the expression on
`Shape`.

**The probe** (a scratch script, not in the tree; the numbers are
this box's):

    form                                        native            routed (pyodide)
    ------------------------------------------  ----------------  ------------------------------
    import Part; base = makeBox(..); base       PermissionNeeded  Solid, 4000 mm3, on the Shape
      bound to Shape, recompute                 (host.import:     property path: WORKS
                                                Part is PROMPT
                                                for a document)
    a = Length*2; b = a + 1mm; b (on a length)  81 mm             81 mm
    def f(x): return x*3; f(Length)             120 mm            "function objects are not
                                                                  supported in the sandbox image"
    lambda, called                              --                the same refusal
    for i in range(1,5): fuse boxes (a stair)   --                Compound, 15000 mm3, 37.5 ms
    while, list comprehension, range, len       --                work
    import FreeCAD / from FreeCAD import Vector --                work (the facade)
    import math; math.pi                        --                works
    extrude, revolve, fuse, cut, common,        --                work
      section, translate, rotate, scale,
      transformGeometry, Placement =, Volume,
      Area, Faces, CenterOfMass, isValid,
      ShapeType, Part.Face/Wire/makePolygon/
      makeCircle/makeCompound/Vertex/Circle,
      ArcOfCircle.toShape
    makeFillet, makeChamfer, mirror,            --                "No attribute named ..."
      makeThickness, Part.makeCylinder,
      makeSphere, makeLoft, makeHelix
    the bracket (two boxes, fuse, cleaned)      --                12-20 ms per evaluation
    guest boot (first evaluation)               --                2.5 s, RSS +335 MB
    second evaluation                           --                0.2 ms
    RSS across three reset()s                   --                462 -> 557 -> 633 -> 659 MB

Four facts fall out.  (1) A document program is ROUTED-ONLY by the
catalog: natively `import Part` is `host.import:Part`, PROMPT for a
document principal, fail-closed -- which is 1.4's rule working, and
means sec 11 item 3 (routing ON by default) and the runtime offer of
2.5 are this feature's precondition; a build with no guest shows the
padlock's download offer, never a native run.  (2) The ONE language
gap in the guest is function objects: `makeFunc` and the `FUNC` value
path throw under `FC_EXPR_IMAGE` (`Expression.cpp:4648`, `:6565`)
because a function escapes the evaluation as an `ExpressionPy`, a
host binding -- and `ExpressionPy` is `__call__` plus `__doc__`
(`ExpressionPy.xml`, `ExpressionPyImp.cpp` 100 lines): the smallest
binding in the tree, left out of `ImageSources.cmake` because the
corpus never stored a function (Phase 0's reverse audit).  Without
it a library is impossible; with it a library is a frame.  (3) The
constructive surface is most of the way there -- `TopoShapePy` 53 of
111 members annotated, 345 annotations across the XMLs (217 call,
108 value, 20 handle), the `Part` facade 33 callables -- and the
holes are a list, below.  (4) A guest is 2.5 s and 335 MB, and a
reset does not give the memory back (about 100 MB retained per
reset, decelerating).

**THE RE-SIZING (2026-09-13), after the proxy chain.**  docs/
ProxyChain.md P1 and P2 are built, and they change what this section
sizes.  A document no longer needs a new carrier class to give an
object CODE: a `Part::FeaturePython` with a `Spreadsheet::Sheet` in
its `ProxyExp` runs that sheet's alias'd cell as its `execute`,
today, with nothing from this section added.  Probed 2026-09-13
(`FreeCADCmd`, this box) -- the flange of (a) written that way,
against the same flange written as a host-Python `Proxy`:

    the flange: six parameters, six bolt holes    native     routed
    --------------------------------------------  ---------  -----------
    first build (Compound, 18749 mm3, 10 faces)   22.0 ms    --
    per parameter change (median of 8)            19.6 ms    --
    a second instance on the same sheet           29.9 ms    --
    the same flange as a host-Python Proxy        21.9 ms    --
      its per parameter change                    21.3 ms    --
    the cell's def evaluated                      works      "function
                                                             objects are
                                                             not supported
                                                             in the
                                                             sandbox image"
    save, reopen, ProxyExp restored, a
      parameter changed, recomputed               works      --
    a method edited on the sheet                  the instances do NOT
                                                  follow (below)

Three things come off it.  (1) **The engine language costs what host
Python costs** -- 19.6 ms against 21.3 ms for the same solid -- because
both spend the time in the same OCCT calls.  The language is not the
price; the kernel is, which is (1.2)'s "the host always recomputes"
seen from the other side.  (2) **D1 is now the gate for BOTH forms.**
The cell's `def` is a function object, and routed it meets the same
refusal a library's would (fact (2) above): the one language gap of
the image is now the chain's gap too, which promotes D1 from this
section's first stage to the precondition of the whole "document
program" idea -- the chain gives a document a way to carry methods
natively, and D1 is what lets those methods run under enforcement.
(3) Everything else the form needs -- the dependency edge, save and
reopen, a link into another file -- is the link property's, built and
gated already (`FeaturePythonChain`, `ViewProviderChain`).

**One thing the chain does not yet deliver.**  ProxyChain.md 2.1 says
an edit to a method recomputes the instances.  Sometimes it does.
`Flange`'s `execute` is never called after the cell is re-typed -- an
instrumented method, a counter incremented in the cell, proves it
stays put -- and the reason is worth getting right, because the first
reading of it was wrong.

The link DOES propagate, and the mechanism is REVISIONS.  Every
`DocumentObject` carries `_revision`, bumped in `DocumentObject::
onChanged` (`DocumentObject.cpp:1045`) when a non-`Output` property of
it is touched.  A link property reports itself touched by comparing:
`PropertyLink::isTouched()` and `PropertyLinkList::isTouched()`
(`PropertyLinks.cpp:789`, `:1107`) are `linkRevision(target) !=
_revision`, the revision each stored at its last `purgeTouched()`.
`Property::testStatus(bits, mask)` substitutes that VIRTUAL
`isTouched()` whenever `Touched` is in the bits or the mask, and
`Document::_recomputeFeature`'s recompute optimization
(`Document.cpp:4673`) asks exactly `testPropertyStatus(Property::
Touched, mask)`.  So a `ProxyExp` change reaches its instances the
moment the linked object's revision moves -- probed: replace the
linked object's `Proxy` and every instance recomputes.

It fails for exactly the two definition changes that move no
revision.  (1) **A `Spreadsheet::Sheet` pins its revision**:
`Sheet::getRevision()` is `return 0`, a literal (`Sheet.h:91`,
"Fix the object revision to reduce effect of recomputation time"),
so no link of ANY kind sees a sheet change -- probed with a plain
`PropertyLink` and an ordinary `PropertyXLinkList` to a sheet, both
equally blind.  This is not a `ProxyExp` problem; it is the
spreadsheet's deliberate bargain, that its consumers are driven by
the expression engine's own cell dependencies rather than by the
link, and a method in a cell is a consumer the bargain did not
foresee.  (2) **A function stored on the linked object from Python**
(`L.expExecute = f`) lands in `dict_methods` through
`FeaturePythonPyT::_setattr`, not in a Property, so nothing is
touched and no revision moves.

Those two are, precisely, two of the three places ProxyChain.md 2.4
bumps the chain's generation counter; the third,
`PropertyPythonObject::hasSetValue`, is the one that already
propagates.

**The fix, RULED 2026-09-13, is NOT the counter on its own.**  Every
coarse answer -- a real `Sheet::getRevision()`, a walk of the carrier's
in-list touching each owner, or `mustExecute()` answered straight from
the process-wide counter -- recomputes every instance when ANY cell of
the sheet moves, because `PropertySheet::hasSetValue()` carries no cell
identity.  That is the cost the pin exists to avoid, and the contract
it protects is `ObjectIdentifier::isTouched()` ->
`resolvedProperty->isTouched()`: a dependency on a sheet is a
dependency on ONE CELL.  So the chain gets a fine-grained test of its
own -- the counter stays as the cheap "something moved somewhere"
gate, and only when it moves does the chain re-resolve and compare the
resolved callable against the one last executed, recomputing only when
THIS feature's definition changed.  The full design, the measurements
and the eight gate cases are ProxyChain.md 4.5; it was D2's first item
and is **BUILT 2026-09-13** -- with one correction, that
`skipRecompute()` and not `mustExecute()` is the gate the sheet edit
has to pass, the carrier's recompute having set `ObjectStatus::Enforce`
on the instance already.  ProxyChain.md 4.6.

**THE TRAP, and it is the spreadsheet's, not the chain's.**  **A
function body's identifiers are NOT dependencies, and the body reads
them LIVE at call time.**  `VariableExpression::_getIdentifiers`
returns early under `_FunctionDepth` (`Expression.cpp:4163`), raised by
`LambdaExpression::_visit` around the body (`:6529`).  Measured: a cell
`=def m(obj): return r * 2` returns 10; `r`'s cell is changed to 9; the
cell is NOT re-evaluated, the function object is the SAME object, and
it now returns 18.  **A method that reads sibling cells therefore
changes behaviour with nothing observable changing -- no revision, no
touched property, no new function object -- and no mechanism in the
engine can see it.**  It is deliberate and it predates the chain (it is
true of any spreadsheet function), so **the idiom is that a method's
inputs come through `obj`, the instance's own properties, never
through the sheet's frame** -- which is what the chain hands every
element anyway.  A shared constant that must live in a cell is bound on
the INSTANCE with an ordinary expression (`obj.Pitch = Sheet.pitch`),
and that IS tracked.

**Three language facts the probe turned up**, all of them traps for
anyone writing these programs (sec 12).  A comma straight after a
digit is part of the NUMBER: `min(1,2)` evaluates to **1.2**, and
`vector(1,2,3)` is a syntax error -- a space after each comma is not
style, it is required.  That one is a lexer rule, the European
decimal comma: `ExpressionParser.l:381`,
`<INITIAL>{DIGIT}*","{DIGIT}+{EXPO}?`, which swallows the separator
before the grammar ever sees an argument list (and being `<INITIAL>`
it does not apply inside python mode).  `range` is NOT a builtin of
the engine language; `for ... in range(...)` works ROUTED, where the
guest is Python and `range` is Python's, and natively the same
program must count with a `while` -- the one place the native twin of
(e) is not a twin, so the corpus rig writes its loops the native way.
And `pi` is a CONSTANT token, so `math.pi` is a syntax error where
`math.sqrt(4)` is 2.0.

**What it does NOT change.**  (b) stands entire -- the surface audit
and the version stamp are what a method may CALL, and a sheet's
method calls the same surface a library's does.  (d) stands: the
shared guest, one principal per document.  (c) is narrowed, not
dropped, below.

**(a) The example set** [re-sized 2026-09-13].  Three programs, each
also the tutorial's text: the **bracket** (two boxes fused and
cleaned, three parameters; runs today), the **stair** (boxes
translated and fused in a loop -- written with a `while`, not `for
... in range`, which the native twin does not have; runs today), the
**flange** (a body cut by a bore and a bolt circle, six parameters, a
`def` per hole pattern -- needs (b) and the function objects).  Each
is written TWICE, and the second form is no longer one thing: as the
expression on a `Part::Feature`'s `Shape`, and as a METHOD on a
linked object -- a library module (c) for the bracket and the stair,
a **sheet in `ProxyExp`** for the flange, so the set covers both
carriers and the gate compares them.  About 200 lines of engine code,
saved as `.FCStd` fixtures for (e).

**(b) The surface audit, and the version.**  To annotate, one XML
line each, `<Sandbox tier="call"/>`: on `TopoShapePy` `makeFillet`,
`makeChamfer`, `mirror`, `makeThickness`, `makeOffset2D`,
`translated`, `rotated`, `scaled`, `generalFuse`, `slice`, `slices`,
`check`, `childShapes`, `removeInternalWires`, and `Shells`,
`CompSolids`, `Compounds` as `handle`; in the `Part` facade's table
(`MODULE_FACADES` in the generator) `makeCylinder`, `makeSphere`,
`makeCone`, `makeTorus`, `makeWedge`, `makeLoft`, `makeHelix`,
`makeTube`, `makeRevolution`, `makeRuledSurface`, `makeSweepSurface`,
`makeFilledFace`, `makeThread`.  Not added, by the OpenSCAD rule of
1.5 (no I/O): `read`, `export*`, `import*`, `makeShapeFromMesh`,
`show`, `open`, `insert`; not added because they are the element
map's own (`mapSubElement`, `getElementHistory`, `searchSubShape`),
which a program addresses through the handle-tier sub-shape lists.
The VERSION: the generator writes `FCX_SURFACE_VERSION` -- an
integer bumped by hand when a member is removed or its meaning
changes, and beside it the sha256 of the sorted annotated set (name,
tier, signature) -- into both `.inc` files and `FreeCAD.
ExpressionSandbox.surfaceVersion()`; a library records `Surface` at
its last edit, a document `Meta["ExpressionSurface"]` at save; on
open a lower integer is logged once per document ("written against
surface 1, this build is 2"), never refused -- FeatureScript's pin is
a promise the host cannot keep for OCCT's own behaviour, so ours is a
record and a warning.  The sizing declined a per-file pinned facade
set: it would mean shipping every past surface.

**(c) The carrier** -- **narrowed 2026-09-13**: no longer "the"
carrier but the NAMED one.  A sheet in `ProxyExp` already carries
methods (the worked example below), so what the library is still for
is the three things a sheet is not: a MODULE namespace reached by
`import` from any expression of the document rather than by a link
from one object; module-level state computed once and shared (the
`hole_d` of the example, a table of sizes) instead of re-evaluated
per cell; and the pinned SNAPSHOT, a definition that survives its
source file's absence.  A sheet gets the live cross-file case free
(`ProxyExp` is a Global-scope XLink list) and the pinned one not at
all.  Both carriers are behind D1 for routing, and both feed the
same surface of (b).  The design below stands as written.

`App::ExpressionLibrary`, a subclass of
`App::TextDocument` (`Text` inherited: the source; the view provider
and its `TextDocumentEditorView` inherited: the authoring editor,
which closes sec 11 item 6 for the library half), plus `Module`
(`PropertyString`, the import name, default the object's `Name`) and
`Surface` (the version of (b)).  Semantics: the source is engine
statements evaluated ONCE in a fresh frame in the document's guest;
the frame's bindings become a module object; `import <Module>` or
`from <Module> import f` in any expression of the SAME document
binds it.  How it reaches the guest: lazily, through the import
miss.  The image's `ImportModules::getModule` (`Expression.cpp:2385`)
already asks the host about a `ModuleNotFoundError`
(`missingImportOffer`, 5.5); a step before that, a `_fcx.libraries`
registry keyed by the CURRENT principal is consulted, and on a miss
a new op `lib.source {name}` asks the host, which answers from the
requesting principal's document -- the library's `Text`, its
`Surface`, a revision counter -- or "no such library", after which
the package offer runs as today.  The guest builds the module
(`types.ModuleType`, the frame's variables as attributes, the engine
function objects of fact (2) among them) under the principal's key,
so two documents' `lib` never meet in the shared guest.  Changes:
the library's `onChanged(Text)` bumps the revision and sends
`lib.drop {name}` for its principal (never `reset()`: fact (4)), and
touches itself.  The dependency edge: `ImportStatement` and
`FromStatement` gain `_getIdentifiers`, resolving a module name to
`<library>.Text` when the owner's document holds a library of that
`Module` -- the DAG orders the library before its consumers, a
`Text` edit recomputes them, and `getDeps` reports it in the pack;
a name that is no library resolves to nothing, as today.

**Across files** **[RULED 2026-09-10: "why restrict same-document
library, that kind of defeats the purpose of having a library if it
can only be used in the same document, so it needs to be copied in
to use?"]**.  The first draft of this section said same-document
only and called it FeatureScript's rule; it is not -- a Feature
Studio imports other documents' studios pinned to a version of that
document, and a pin is what makes copying unnecessary.  What the
wall was protecting does not need it: privilege, because a document
principal cannot escalate (geometry calls are ALLOW for everyone,
a foreign read prompts, GUI and host imports are DENY or PROMPT,
the budget bounds the cost), so a hostile library gains nothing a
program typed into the importer could not do, and the guest's
per-principal module key already attributes the code to its origin
(1.5's taint rule); and integrity, because a library file changing
under its consumers is a supply-chain problem, answered by a pin,
not a wall.  So a library in another file is reached the way this
fork reaches another file's objects: through an external link.
`App::ExpressionLibrary` gains `Source` (`App::PropertyXLink`, a
library object in another document) and `Pinned` (`PropertyBool`)
with `Snapshot` (`PropertyString`).  A LINKED library proxies the
source's `Text` and takes its own `Module` name (the consumer's
choice; relabeling is local); the link machinery loads the other
file, follows a rename, and reports a missing file as any external
link does; the one `doc.foreign` prompt happens when the link is
made, the existing cross-document wall, and never per evaluation.
LIVE (`Pinned` false): the consumers follow the source; an edit
there recomputes here, the dependency edge of the previous
paragraph crossing the link (consumer -> the linked library -> the
source library's `Text`, through the XLink's own out-list).  PINNED:
`Snapshot` holds the text and `Surface` the version at the pin; the
file opens and recomputes with the source file absent, which is what
makes it shareable; unpinning takes the source's current text, and
the editor shows the diff.  Copying a library in is the degenerate
case of a pin with the link dropped.  In the shared guest a module
is keyed by the principal of the document whose text it IS -- the
source's for a live link, the consumer's for a pin (the snapshot's
text joins the consumer's hash) -- so ten consumers of one source
build the module once.  The principal: `DocumentHashBuilder` collects
expression containers only (`ExpressionSecurityRuntime.cpp:318-
340`); the collector gains the libraries' `Text` through the unused
`addScript(moduleClass, code)` (`ExpressionSecurity.cpp:386`), so a
tampered library voids the document's grants exactly as a tampered
expression does; a pinned snapshot joins the consumer's hash, a
live link's text joins the source's, and only the source's.  The
native twin, for the parity rig only: native `ImportModules::
getModule` resolves a library the same way -- own, linked live, or
pinned -- by evaluating its text in a native frame and wrapping the
frame as a module: the path the gate compares against, with
enforcement off as the corpus rig runs; under enforcement it never
runs, by fact (1).  Python: `FreeCAD.ExpressionSandbox.
libraries(doc)` for tests.

**A worked example: how a library is stored, entered and called.**

The library.  A document `Brackets.FCStd` holds one object, `Lib`,
of type `App::ExpressionLibrary`, with `Module = "brackets"` and
this `Text` (the engine's language; it is what the inherited text
editor shows when the object is double-clicked):

    import Part

    hole_d = 4mm

    def bracket(L, W, T):
        base = Part.makeBox(L, W, T)
        wall = Part.makeBox(T, W, L)
        return base.fuse(wall).removeSplitter()

    def holes(shape, d, count, pitch, z):
        for i in range(count):
            shape = shape.cut(Part.makeCylinder(d / 2, z,
                                                vector(10mm + i * pitch, 10mm, 0)))
        return shape

(`range` there is the GUEST's -- Python's builtin, visible because the
guest is Python.  The engine has no `range` of its own, so the native
twin the parity rig runs counts with a `while`; probed 2026-09-13, and
in sec 12.)

How it is stored.  A property, nothing else: in the file's
`Document.xml`, the object's `Text` is a `PropertyString` holding
the source verbatim, beside `Module`, `Surface` (the version stamp
of (b), say `1`) and, for a linked library, `Source` (the XLink's
file and object), `Pinned` and `Snapshot`:

    <Object type="App::ExpressionLibrary" name="Lib">
      <Properties>
        <Property name="Text" type="App::PropertyString">
          <String value="import Part&#10;&#10;hole_d = 4mm&#10;..."/>
        </Property>
        <Property name="Module" type="App::PropertyString">
          <String value="brackets"/>
        </Property>
        <Property name="Surface" type="App::PropertyString">
          <String value="1"/>
        </Property>
      </Properties>
    </Object>

The entry point.  There is none in the `main` sense.  The text is a
module: its statements run ONCE, top to bottom, in a fresh frame in
the document's guest, the first time any expression of that document
imports `brackets` (the guest's import miss asks the host
`lib.source {"name": "brackets"}`, the host answers with the text
of the library whose `Module` matches in the requesting principal's
document, and the guest binds the frame as the module).  What the
run leaves in the frame -- `bracket`, `holes`, `hole_d`, and `Part`
-- is the library's API.  A module-level statement can compute (a
table of sizes, a constant from a formula) but cannot act: no I/O,
no document writes, no GUI, by the catalog.  The run repeats only
when the text changes (the library's `onChanged(Text)` bumps its
revision and drops the module with `lib.drop`; the next import
rebuilds it) or after a guest reset.

How other code invokes it, in the same file.  A `Part::Feature`
named `Bracket` with three properties the user added -- `Length`
40 mm, `Width` 20 mm, `Thick` 5 mm -- and this expression bound to
its `Shape` (typed in `DlgExpressionInput`, multi-line; stored in the
object's `ExpressionEngine` property as `<Expression path="Shape"
expression="..."/>`):

    from brackets import bracket, holes
    import brackets
    holes(bracket(Length, Width, Thick), brackets.hole_d, 3, 12mm, Thick)

On recompute the engine evaluates this with a call frame
(`PropertyExpressionEngine.cpp:851`), routed: the `from` statement
resolves through the module registry, the two calls run in the
guest, each `Part.*` call crosses as `geom.call` with handle
arguments, the final shape comes back as a handle and lands in
`Shape` through `setPyObject`.  `Length` changed in the property
editor -> `Bracket` recomputes.  `Lib.Text` edited -> `Bracket`
recomputes, because `from brackets import` gave `Bracket` a
dependency on `Lib.Text`.  A spreadsheet cell does the same on one
line, `;` separating simple statements:

    =import brackets; brackets.bracket(A1, A2, A3).Volume

and a second feature reuses the library with a different body:

    from brackets import holes
    holes(Part.makeBox(Length, Width, Thick), 6mm, 2, 20mm, Thick)

(`Part` here is bound by the library's own `import Part` only inside
the library; the consumer names it itself with `import Part` on a
line above, or writes `brackets.Part` -- the sizing keeps modules
honest rather than injecting names.)

How another file invokes it.  `Assembly.FCStd` adds its own
`App::ExpressionLibrary`, `LibB`, with `Source = Brackets.FCStd#Lib`
(the XLink, made through the property editor's link picker or
`LibB.Source = Brackets.getObject("Lib")` in the console, the one
`doc.foreign` prompt answered then), `Module = "brackets"` (or any
name the assembly prefers), `Pinned = true`, and `Snapshot` filled
from the source at that moment.  Every expression in `Assembly.FCStd`
imports `brackets` exactly as above; the file opens on a machine that
has never seen `Brackets.FCStd`, because the snapshot is the text.
Unpin, and the assembly follows the source: with `Brackets.FCStd`
open, an edit to `Lib.Text` recomputes the assembly's brackets;
absent, the link reports its missing file as an external link does
and the consumers' expressions fail with "no library brackets" until
it is found.  Live or pinned, the guest keys the module by the
principal whose text it is, so the assembly and the source document
share one built module when both are open on a live link.

**The same thing without a library: a sheet as the type** [probed
2026-09-13, it runs].  The flange, in one file, in two objects.

`Type`, a `Spreadsheet::Sheet`.  One cell, `A1`, aliased
`expExecute`; its CONTENT is this text behind a leading `=`, and its
VALUE, once the sheet recomputes, is the function object `build`:

    def build(obj):
        import Part
        body = Part.makeCylinder(obj.Dia / 2, obj.Thick)
        body = body.cut(Part.makeCylinder(obj.Bore / 2, obj.Thick * 3,
                                          vector(0, 0, -obj.Thick)))
        i = 0
        while i < obj.Bolts:
            a = i * 360deg / obj.Bolts
            body = body.cut(Part.makeCylinder(
                    obj.HoleDia / 2, obj.Thick * 3,
                    vector(obj.Pcd / 2 * cos(a),
                           obj.Pcd / 2 * sin(a), -obj.Thick)))
            i = i + 1
        obj.Shape = body
        return True

    build

`Flange`, a `Part::FeaturePython` with six properties the user added
(`Dia` 60 mm, `Thick` 8 mm, `Bore` 20 mm, `Pcd` 44 mm, `HoleDia`
6 mm, `Bolts` 6), `Proxy` unset, and `ProxyExp = [Type]`.  That is
the whole construction: no Python class, no module, no import
anywhere but inside the method.  On recompute the chain resolves
`Type.expExecute` (ProxyChain.md 2.2, path 2 -- the alias is an
ordinary attribute of the sheet), calls it with the feature, the
method writes `obj.Shape`, returns True, and the chain stops before
the C++ base.  A second `Part::FeaturePython` linking the same sheet
with different numbers is a second instance of the same type; the
sheet is the type, and `ProxyExp` is the type link (ProxyChain.md
2.5).

Four differences from the library form, none of them accidental.
The method WRITES `obj.Shape` where the expression form RETURNS a
shape the engine lands on the bound property -- a document write,
classified `doc.write.self` at the assignment itself
(`ObjectIdentifier.cpp:2041`, ALLOW for the owning document by 3.2's
same-origin rule and the `doc.foreign` prompt across files), and
natively it logs
"Object property assignment may break dependency tracking"
(`Expression.cpp:1303`) once per assignment per recompute, which is
the honest warning that the engine cannot see through an assignment
to what the feature now depends on.  The method form reaches the
WHOLE hook surface -- `expOnChanged`, `expMustExecute`, and through
`ViewProxyExp` the icon and the tree -- where the expression form
reaches one property's value.  Its parameters are the INSTANCE's own
properties, per-instance state the type does not own, where the
library's are the caller's arguments.  And sharing across files
needs nothing new: `ProxyExp` is an `App::PropertyXLinkList` of
Global scope, so a sheet in another document is linked the way any
external link is -- which is (c)'s `Source`/`Pinned`/`Snapshot`
machinery arriving free for the method case, and NOT arriving for
the pinned-snapshot case, where a consumer wants the definition to
survive the source file's absence.


**(d) Per-document guests: NO.**  335 MB and 2.5 s per guest against
the 1.5 s the item expected, and a reset that keeps about 100 MB:
ten open documents would be 3 GB.  The shared guest stays, with the
per-principal module namespace of (c) doing the isolation the guest
boundary would have done for names; principals are per evaluation
already (`ImageHost::evalExpression`'s owner scope).  What a shared
guest cannot isolate -- one document's runaway allocation -- is the
memory ceiling of sec 13, now sized as D4 below; crash isolation is
the multi-process OCCT direction's (`docs/ComputeBoundaries.md`),
not the guest's.  The reset retention is a new trap (sec 12) and a
D4 measurement.

**(e) The gate.**  `SandboxProgram` (a Python gate module in the
default list beside `SandboxCorpusGui`): the three fixture files
open with routing ON, recompute, and each `Shape` is BRep
byte-identical to the same file recomputed native with enforcement
off; a parameter change recomputes to the native answer; save,
reopen, equal; the library's `Text` edited -> consumers recompute;
the `Text` tampered on disk -> a new principal, the file's grants
gone; a fourth fixture pair, the bracket library in one file and a
consumer in another, opened linked live (edit there, recompute
here), then pinned and opened with the source file absent (the
snapshot serves, the shape equal), then unpinned with the source
back (the diff shown, the text taken).  A gtest `ExpressionImageHost.programs` for the function
objects (a `def` evaluated routed, called, passed as a value, a
lambda in a comprehension) and the surface version.  The corpus
gate stays green with the three fixtures added to its list.  The
bench of sec 11 item 4 runs the flange routed against native as its
one shape program.

Added by the re-sizing: a fifth fixture, the flange as a
sheet-extended `Part::FeaturePython` (the worked example above),
whose `Shape` is BRep-identical to the library form's and to the
native run; two instances on one sheet, each with its own
parameters, both correct; and the case the probe failed -- the
method edited in the cell, then a plain `doc.recompute()`, with both
instances rebuilt; and beside it the negative that the ruling turns
on -- an UNRELATED cell of the same sheet edited, no instance
recomputing -- with the plain-spreadsheet regression that no
`ProxyExp` is involved in at all: an unrelated cell touched leaves
the other cells' consumers alone.  All of it must run with
`OptimizeRecompute` at its default ON, because with it off every case
passes for the wrong reason.  The
`FeaturePythonChain` gate of ProxyChain.md stays green beside it,
and its sheet cases are what P3 re-runs routed.

**D1, BUILT 2026-09-13.**  What landed, and what the gate found that
the sizing did not.

`ExpressionPyImp.cpp` is in `ImageSources.cmake` and both throws are
gone (`makeFunc`, the FUNC value path).  In the image a function
object cannot hold its owner's Python face the way the host's does:
the owner is the adapter object of ONE `EvalTransaction`, whose
`getPyObject()` is a handle proxy, not a `PyObjectBase`.  So it records
the transaction's serial, and `ownerAlive()` asks
`EvalTransaction::alive()`, which walks the nesting chain -- a
transaction now restores the one it interrupted, where it used to
clear `_current` under an evaluation still running.  Called after its
evaluation (kept in a guest global), it raises the host's own "Owner
document object expired".

As a FINAL value it is still refused -- "result of type
'App.Expression' does not marshal by value" -- which is right for this
stage and is precisely P3's problem: a sheet cell whose value is a
`def`, evaluated routed, is a function that cannot leave the cell's
evaluation, so the chain's sheet form under routing needs the method
either to cross as a guest reference (the `gmethod` shape of 3.2) or to
run inside an evaluation of its own.  That is D2, with P3, not D1.
Probed: natively a cell `=def m(obj): ...` holds `<Function m>` and
`=m(1)` beside it gives 6; routed, the first reads the marshal error
and the second "Expects Python callable".  (Lifted by P3, 2026-09-13,
below: the function crosses as a stand-in.)

The pack now carries what a function body reads
(`App::FunctionBodyIdentifiers` around `getIdentifiers()`,
ExpressionImageHost.cpp).  Those names are still NOT dependencies --
the trap of sec 12 stands untouched, dependency tracking never sees the
bodies -- but a body reading `HoleDia`, which nothing outside the body
reads, found no binding in the guest and failed to resolve.  Natively
the body resolves it live; the guest has only the pack.

The surface (b): 14 `call` and 3 `handle` annotations on `TopoShapePy`
and 13 constructors in `MODULE_FACADES["Part"]`, as listed; 362
members across 57 facades.  The stamp: `FCX_SURFACE_VERSION = 1` in
the generator and the sha256 of the sorted set (type key, member,
kind, tier; module, name, kind, permission -- a `Methode` declares no
argument list in the XML, so that is all the signature the table
has), written into BOTH `.inc` files.
`FreeCAD.ExpressionSandbox.surfaceVersion()` returns the host's;
`surfaceVersion(True)` boots the guest and adds its `_fcx.surface()`
-- a mismatch is a wheel built from other annotations, which nothing
else would notice until a member failed to cross.  The document
record: `Meta["ExpressionSurface"]`, set at `signalStartSaveDocument`
on a document carrying at least one expression and never on one
carrying none; at `signalFinishRestoreDocument` a lower integer is a
one-line warning.  Connected in `Application::initApplication()` after
the singleton exists (`initPyModule` runs inside the constructor).  The
library's own `Surface` record waits for the library (D2).

Two bugs older than any of this, both found by the flange gate and both
in sec 12.  Every `while` loop in the image ran exactly ONCE -- a
compiled-out single-statement `if` took the `continue` as its body --
so the routed flange came back with one bolt hole of six (5 faces,
19880 mm3).  And a called lambda printed without its parentheses:
`(lambda k: k * k)(i)` printed as a lambda whose body is `k * k(i)`,
natively on save and on every routed evaluation, which ships the
printed form.

Gate, in `tests/src/App/ExpressionImageHost.cpp`: `programs` (six
programs routed = native -- a def called, a function passed as a
value, a lambda in a comprehension, a body-only identifier, defaults
and keywords, a list of lambdas); `programsFunctionValueStaysInTheGuest`;
`programsFlangeMatchesNative` (routed under enforcement, native with
enforcement off: a Compound, 10 faces, equal `Volume`, and
`exportBrepToString()` BYTE-IDENTICAL -- the guest's `cos` and `sin`
gave the host's doubles for these six angles);
`programsSurfaceStampMatchesHost`; `programsSurfaceRecordedAtSave`.
`partModuleFacade`'s undeclared-name example moved from `makeSphere`,
now declared, to `read`, which never will be.  Suites green at the
build: C++ 610/610 (offscreen), Python 2715 OK, `FeaturePythonChain`
27 OK.  The cost: 573 lines added and 39 removed across 18 files --
ABOVE the 290-490 sized, though 223 of the added are the tests, which
the sizing's D1 row did not count, and 29 are the two old bugs; about
320 lines of the feature itself.

**D2's library half, BUILT 2026-09-13.**  `App::ExpressionLibrary`
(`src/App/ExpressionLibrary.h/.cpp`): a `TextDocument` with `Module` and
`Surface`, the native module, the guest registry with `lib.source` and
the drops, the dependency edge, the principal hash, `libraries(doc)` --
for a library of the importer's OWN document.  The linked library
(`Source`, `Pinned`, `Snapshot`) and P3 are the rest of D2.  Six things
the build found that the sizing did not.

1. *The engine's functions are dynamically scoped*, so "the frame's
   bindings become a module object" was one step short.  A function
   captures nothing (`makeFunc` copies the body) and a free name is
   looked up through every frame on the stack at call time, which means
   a library function would see each CONSUMER's locals and not its own
   module's `hole_d` or the helper defined below it.  A library's
   functions carry the module's dict (`CallableExpression::setGlobals`,
   attached by `makeFunc` while a module builds or while one of its
   functions runs) and are called on an evaluation stack of their own
   whose base frame holds that dict (`EvalStackSwap`).  The build is
   isolated the same way: a module's statements see nothing of the
   evaluation that imported it.  Every other function keeps the old
   rule.  Sec 12.
2. *The default module name cannot be the Name.*  `Lib.f` parses as a
   property of the object `Lib` before any frame is asked, so a module
   named like its object is unreachable with a dot.  An empty `Module`
   answers to the Name with its first letter lower-cased.  Sec 12.
3. *A library importing a library.*  Dropping `brackets` clears its
   dict, and `more`, which bound `brackets` at its own build, would go
   on holding the empty module.  Each built module records which BUILD
   of each library it imported and rebuilds after any of them does --
   natively on `ExpressionLibrary`, in the guest on the registry entry.
   And the edge is transitive: `importedModules()` parses a library's
   text once per revision and the consumer depends on every library
   reached, so an edit to `brackets` recomputes a consumer that only
   names `more`.  Found writing these notes, before any test did.
4. *A kept module outlives the evaluation D1's functions were tied to.*
   A D1 function records its transaction and its owner IS that
   transaction's adapter object, so a module cached across evaluations
   would hold dead owners.  A guest module gets an adapter document and
   object of its own, and a serial that stays alive while the guest
   keeps the module and some evaluation is running
   (`EvalTransaction::setLibraryAlive`, `LibraryBuild`).  A replaced or
   dropped module is retired, not freed -- a function of it may be on
   the stack of the evaluation that replaced it -- and retired modules
   are swept, their dicts cleared and serials killed, at the next
   top-level request; the kept owners' cached pack properties are
   forgotten there too, since every evaluation brings a new pack.
5. *The registry is asked through the request, not on every import
   miss.*  The sizing had the guest send `lib.source` for any module it
   could not find, which would be a round trip for every `import Part`.
   An evaluation owned by a document that holds libraries carries
   `libs: {module: [key, revision]}`; an import naming no library never
   crosses, and `lib.source` is sent on a module's first use and after
   its revision moves.  `ld: [[key, module]]` rides the next request
   after a text change or a removal.  Measured in the gtest: three
   evaluations, one `lib.source`; an edit, one more; `import math`,
   none.  The key is the principal of the document whose text it is;
   since the text is part of that hash, an edit moves the key as well as
   the revision.
6. *A comment-only line is a syntax error* outside python mode, a
   lexer rule older than any of this (sec 12).  A library cannot open
   with a comment block until the committed lexer is regenerated.

Where it sits.  `ImportModules::getModule` asks for a library FIRST --
before the permission check, which would read `brackets` as a host
import, and outside the process-wide import cache, where two documents'
`brackets` would meet (`ExpressionLibrary::importModule` natively,
`FcxImage::libraryModule` in the guest).  `ImportStatement` and
`FromStatement` gain `_getIdentifiers`: `Text` and `Module` of every
library reached, so a rename recomputes the consumer too, which then
fails as it should.  The bindings pack skips those identifiers -- the
text crosses through `lib.source` once per revision, never per
evaluation.  The principal gains the text through `addScript`, and the
cached principal is voided on a `Text` or `Module` change as on an
expression's.  `Surface` is written at each text edit; a lower integer
is warned once at open.  `execute()` parses the text, so a syntax error
shows on the library rather than first on some consumer.

Limits of this half, stated.  In the guest a library's module-level
code and its functions see no document identifiers: the pack is the
consumer's, and a library is meant to be handed its inputs.  Natively
they resolve against the library object, so the parity rig stays on
programs that take their inputs as arguments.  A duplicated module
name: the first library in object order wins, silently.  A library is
reached only from its own document until `Source` lands.

Gate: `SandboxProgram`, a new Python module in the default list, 19
native cases -- import and from-import, the default name, a function
resolving its module and refusing its caller's names, blank lines and a
trailing comment, a comment-only line refused on the library, a library
importing a library and following an edit to it, a circular import, a
syntax error on the library, two documents with the same module name,
the edge (and none for `import math`), a text edit and a rename
recomputing, save and reopen, `libraries()`, the principal moving with
the text, the surface stamp.  And five gtests routed = native
(`ExpressionRoutingTest.libraries*`): import, from, a constant, a call
in a comprehension, and the caller's names refused both ways; one
`lib.source` per revision and none for a plain import; two documents
keeping their own module; an imported library following an edit; the
bracket library's shape BRep byte-identical.  Suites green at the
build: C++ 615/615 (offscreen), Python 2734 OK, `FeaturePythonChain` 27
OK, the view gate (`ViewProviderHooks`, `ViewProviderChain`) 25 OK.
Code: b34eb7ca59.  The cost: 968 lines added
and 5 removed in the feature, 439 in tests, against 500-730 sized for
these rows -- the overrun is the scoping of (1), the kept modules of
(4) and the transitive rebuild of (3), none of them in the sizing.

**The linked library, BUILT 2026-09-13.**  `Source` (an
`App::PropertyXLink`), `Pinned` and `Snapshot` on
`App::ExpressionLibrary`.  The HOLDER is the library whose text is
used: the object itself when unlinked or pinned, else the end of its
live links (`getHolder()`, which names its reason for a link that does
not resolve, one that is no library, or a cycle).  Five things the
build settled that the design had left open.

1. *A live link is the source's module, and its imports resolve in the
   source's file.*  Natively a linked library hands `getModule()` to
   the holder, so every consumer of one source shares one build, and
   the holder's text builds with the holder as owner: an `import
   helpers` inside it finds the SOURCE document's `helpers`, which the
   consumer's document need not hold.  Routed, the same.  The guest keys
   the module by the holder document's principal -- two documents
   linking one source under one name ask `lib.source` once, measured --
   and a kept module remembers its home document and that document's
   import table, both from the `lib.source` reply.  An import made by
   the module's own code resolves in that table and names the home as
   `k` on its `lib.source`, which the host serves only for a document a
   live link reaches from the owner's (`reachesHome`).  A pinned
   snapshot's imports resolve in the CONSUMER's document, since the
   source may be absent: pinning a library that imports a library means
   the consumer holds that one too.
2. *The import table rides as triples.*  A `lib.source` reply is decoded
   as a wire value, where a map keyed by module name would read a module
   named `t` -- the tag key -- as a typed value.
3. *The edge walks the links.*  An import depends on `Text`, `Module`,
   `Source`, `Pinned` and `Snapshot` of every library from the one named
   to the holder (cross-document identifiers for the source's), and on
   what the holder's text imports, resolved in the holder's document.
   Probed both ways: the source edited with only the consumer's document
   recomputed, and with the source's recomputed first; the consumer
   follows in both.
4. *The principal holds the link, not the text.*  An unlinked library
   contributes its `Text`, a pinned one its `Snapshot`, a live link the
   link as stored (`link:<file>#<object>`).  The text is the source
   document's code and joins that hash only, so an edit in the source
   leaves the consumer's grants alone, while retargeting the link, or
   pinning, is a change of the consumer's own code and voids them.  A
   source changing under a live link is the supply-chain case the pin
   answers (the ruling above), not a wall.
5. *A missing source fails on the library.*  `execute()` reports
   "Source not found: <file>#<object>"; the import raises the same
   reason natively and routed; the consumer waits Touched, as it does
   behind any failed dependency, rather than turning Invalid itself.

The relabel is local by construction: `Module` is the consumer's, and
the source renaming its own module changes nothing downstream.  Pinning
takes the holder's text and its `Surface`; unpinning clears `Snapshot`
and follows the source again; a new `Source` while pinned re-takes the
snapshot, and a pin with nothing to take from keeps the snapshot it has
-- the copied-in library.  `Text` is read-only while linked or pinned.
NOT done: the editor's diff at unpin (a GUI item), and the text editor
still shows the object's own `Text` rather than the holder's.

Found on the way, older than any of it, and FIXED the same day (sec 12):
closing a document an XLink points into, while either document had never
been saved, left the link holding the deleted object.

Gate: `SandboxProgram` gains 12 native cases (`SandboxProgramLinkedCases`):
the live import and the listing, an edit followed in both recompute
orders, the consumer's module name surviving the source's rename,
imports resolving in the source's file and following an edit there, two
links sharing one module, a `Source` that is no library, a reopen
loading the source file, live with the source file deleted, the pin's
snapshot and surface, unpin following the source, pinned opening
without the source, and the principal (a source edit moves nothing;
pinning and retargeting do).  Five gtests routed = native
(`ExpressionRoutingTest.librariesLinked*`, `librariesPinnedMatchesNative`,
`librariesLinkUnresolvedFailsBothWays`): live with an edit, two
documents and one `lib.source`, imports in the source's file with an
edit there, pinned, and the unresolved link failing with its reason
both ways.  Suites green at the build: C++ 620/620 (offscreen), Python
2746 OK, `SandboxProgram` 31 OK, `FeaturePythonChain` 27 OK.  The
cost: about 445 lines of the feature and 370 of tests, against 150-250
sized -- the overrun is (1), the source document's import table in the
guest and the host, which the sizing never saw.

**P3, BUILT 2026-09-13.**  A function value leaves a routed evaluation,
and the proxy chain calls it -- the sheet carrier routed.  Five things it
settled, one ruling it rests on, and an older bug it had to fix first.

1. *A function crosses as a stand-in, and a call is an evaluation of
   its own* -- the design leaning, taken.  The guest answers a function
   as the WHOLE value of an evaluation with `{"t":"gfunc", "n":name}`
   instead of the marshal refusal, and the host builds a
   `FreeCAD.ExpressionSandbox.RoutedFunction` holding the evaluation's
   owner (its Python face, held as a native `ExpressionPy` holds it),
   the source the router shipped and the eval options
   (`makeRoutedFunction`, `ExpressionGuestProxy.cpp`).  Calling it is
   `ImageHost::callFunction`: the same eval request with `call: {a, k}`,
   the guest evaluating the source again inside that transaction and
   calling the value, the result by value.  So the body reads what is
   current at the call, as natively (the trap of sec 12 is untouched:
   still no dependency); the stand-in holds no guest state and survives
   a reset; its repr is native's `<Function name>`.  The price is one
   round trip and one evaluation of the cell's source per call -- for a
   `def` cell that source is the definition, not the body.
2. *Another evaluation calls it over `fcall`.*  A stand-in reaching the
   guest as a binding is `{"t":"gfunc", "id", "n"}`, decoded to a
   `HostFunction` whose call is one `fcall` hop; the host calls the
   stand-in behind the handle and refuses `fcall` on anything else -- a
   handle is not a licence to call the host object behind it.  So
   `=triple(5)` beside `=def triple(x): return x * 3` is routed = native.
   The nested call is a nested evaluation, and its handles live in the
   outer transaction's table.
3. *The chain's call runs as the feature's file* **[RULED 2026-09-13,
   on the P3 cross-file write: "Run as the feature's file"]**.
   `PyHookImp::resolveChain` binds a stand-in it resolves to the object
   the chain extends (`bindRoutedFunction`, a `RoutedChainFunction`; for
   a view hook the object the view provider shows, not gated by a view
   case yet).  A bound call makes that object the one the guest may
   write (`HandleTable::setOwner`) and pushes its document's principal
   for the round trip WHATEVER scope is active (`Runtime::Scope(runAs,
   via)`), so a method linked from another file writing `obj` is a
   same-file write under the feature's grants -- 1.5's taint rule
   loosened for chain calls, knowingly, where the plan had the
   `doc.foreign` prompt.  The pack is NOT the feature's: it is the
   definition's own frame (the sheet's cells a body reads), resolved
   under the sheet as it was when the function was made; only the round
   trip -- every bridge op the body makes -- runs as the feature.  And
   only the chain's call: the same function called from another cell or
   the console runs as its own file, where a feature of another document
   is out of reach, a PermissionError (gated).
4. *The audit line names where the code came from.*  A denial or prompt
   under a bound call logs `<doc>:<feature> via <doc>#<sheet>`
   (`scopeContext`) -- the plan's "the audit line names the linked
   object's document", in the shape the ruling left it: the principal is
   the feature's, the code is the sheet's.  Probed: a method calling
   `FreeCAD.newDocument` is refused (`app.write`, DENY for a document)
   and `audit.log` records `"context":"AuditInstance:Feature via
   AuditType#Type"` under the instance document's principal.
5. *The recompute of ProxyChain.md 4.6 needs nothing.*  The chain keeps
   the UNBOUND stand-in as its identity; a re-evaluated cell holds a new
   one and an untouched cell the one it had, so an edited method rebuilds
   its instances and an unrelated cell rebuilds none, routed as natively
   -- the eight cases, re-run.

Found first, older than any of it, FIXED the same day (sec 12): an
expression that is one compound statement with its body on the same
line -- `def f(obj): obj.Marker = 10`, `if x: y` -- printed without its
line end and did not parse again.  Every routed one-line method cell
failed on it, since the router ships the printed form, and natively such
a sheet cell saved to a file failed on reopen.

Limits, stated.  A call's result crosses by value: a function returning
a function (a curried lambda) is refused as before, and so is a function
NESTED in a value (`[lambda x: x]`) -- only the whole value crosses.  A
stand-in made while routing was on calls routed after routing is
switched off, until its cell is evaluated again.  A stand-in kept in a
guest global outlives its handle and fails as a stale handle does.
Natively a chain call is unchanged -- it runs under whatever scope the
evaluation entry pushes, the sheet's -- since the ruling concerns the
enforced path, and natively a document program is fail-closed on its
imports anyway (fact (1)).

Gate.  gtests, routed: `ExpressionRoutingTest.
programsFunctionValueCrossesAsAStandIn` (replacing D1's
`programsFunctionValueStaysInTheGuest`: the repr, a call made after the
evaluation's handles are gone, one evaluation per call, a body reading
`Width` changed after the function was made -- both a lambda and a def),
`programsStandInCalledFromAnotherEvaluation` (one `fcall`, and refused on
a handle that is no function), `programsChainCallRunsAsTheObjectItExtends`
(unbound: PermissionError and nothing written; bound: the other
document's object written); `ExpressionStatementPrint.
oneLineCompoundStatementParsesAgain`.  `FeaturePythonChain` 27 -> 44 OK:
`RoutedSheetChainCases` and `RoutedChainRecomputeCases` (the sheet cases
and the eight recompute cases, routing on, skipped with no guest),
`RoutedChainCases` (the method a `RoutedFunction` and the recompute
crossing; a method in another file running as the feature's file and
refused when called directly; the sheet flange of the worked example
routed = native, BRep byte-identical), and
`SheetChainCases.testCellCallsAFunctionCell`, native and routed.
Suites green at the build: C++ 624/624 (offscreen), Python 2763 OK,
`SandboxProgram` 31 OK, the view gate (`ViewProviderHooks`,
`ViewProviderChain`) 25 OK.  The cost: about 550 lines of the feature (the printer fix 19
of them) and 385 of tests, against 80-150 sized -- the overrun is the two
stand-in types and the call path through the host, which the row did not
see; the row sized only the tests.

**D3, BUILT 2026-09-14.**  The fixtures, the gate completed around them,
the corpus list, the bench of sec 11 item 4 and the tutorial
(docs/DocumentPrograms.md).  Four things it found, two of them older than
7.17.

The fixtures are `src/Mod/Test/TestData/SandboxProgram`, six files written
by `SandboxProgramFixtures.writeFixtures()` from the texts that module
holds -- the one source; the tutorial quotes them and a native gate case
checks the committed files still carry them.  `ProgramBracket` (the library
`brackets`, `BracketExpr` with the program on its `Shape`, `BracketLib`
importing it), `ProgramStair` (the same pair for the stair), `ProgramFlange`
(the flange as one expression), `ProgramBracketsSource` and
`ProgramBracketsConsumer` (the pair of the sizing's fourth fixture, the link
stored relative so the two move together), `ProgramFlangeSheet` (the sheet
`Type` and two instances, `Flange` and `Flange2`, with their own numbers).
They install as a directory of their own, since the consumer names its source
beside it.

1. *A routed local read through a dot failed in a document named longer
   than 15 characters* -- a reference into a temporary `String`, so the
   freed name read as a foreign document and the host's failure to resolve
   the local was shipped as the answer.  Every test document before had a
   short name; `ProgramFlangeSheet` and a re-saved `saved-ProgramBracket`
   did not, and every sheet case and every save-and-reopen failed routed on
   it.  FIXED (`referencesForeignDocument`, ExpressionImageHost.cpp); sec 12.
2. *Routed = native to the byte was the angles' luck.*  The guest's `sin`
   rounds differently from the host's for some arguments, one ULP;
   `sin(240deg)` is the one D1's flange never met at a 44 mm bolt circle and
   meets at 40 mm.  The committed fixtures are held to the byte, a parameter
   change to 4 ULPs (`assertSameGeometry`); sec 12.
3. *The two forms are the same solid, not always the same bytes.*  The
   bracket's and the stair's expression and library forms are
   byte-identical -- once the stair's library starts from a plain box, since
   a `translated(vector(0, 0, 0))` step leaves a location record of its own.
   The flange's are not: the method writes `obj.Shape` on a
   `Part::FeaturePython`, whose placement leaves a location record the
   expression's `Shape` does not carry.  Every vertex and the volume agree
   exactly, and that is what the case asserts.
4. *The corpus rig had never compared a spreadsheet cell.*  It asked
   `obj.cells.getUsedCells()`, which `PropertySheet` does not have, and its
   `except` made every sheet contribute nothing -- since the rig's first
   commit (`07d5134793`).  And it compared a shape by its repr, which carries
   an address, and a function by its type's name, which is a stand-in's
   routed.  FIXED: the sheet's own `getUsedCells()`, a shape as its type and
   the sha1 of its BRep, a function as its repr.  `run_gate.sh` no longer
   requires `.conda/limited.sh`, which this box no longer has.  Sec 8.2.

The gate.  `SandboxProgram` 31 -> 46 OK.  `SandboxProgramFixtureTextCases`
(3, native): the committed files carry the module's texts, the programs
compared as printed, the libraries verbatim, the sheet's cell, the
parameters and the pair's link.  `SandboxProgramFixtureCases` (12, routed,
skipped without a guest), every one on a copy of the fixtures with
`OptimizeRecompute` forced ON: the four program files routed = native byte
for byte; the linked pair likewise, and equal to the bracket; the two forms
agreeing, with the faces and volumes pinned; a parameter change per file
routed = native within 4 ULPs, and really changing the shape; save and
reopen equal, the pair included; the library's `Text` edited recomputing
its consumer and leaving the expression form alone; the library tampered in
`Document.xml` on disk giving a new principal with none of the file's
grants -- natively under enforcement an `always` grant of `host.import` Part
lets the programs run, and the tampered file's programs are refused; the
live pair following an edit in the source; pinned, opening and recomputing
with the source file deleted; unpinning taking the source's edited text;
on the sheet, an unrelated cell rebuilding neither instance and an edited
method rebuilding both, each with its own numbers; and the plain
spreadsheet regression on the same sheet.  gtests:
`ExpressionRoutingTest.localMemberInALongNamedDocumentStaysLocal`, and
`ExpressionImageBenchTest.DISABLED_BenchFlangeProgram` (sec 8.1: 22.0 ms
routed, 19.6 ms native); the flange's text and parameters are helpers shared
with `programsFlangeMatchesNative`.  The corpus rig over the fixtures: 6
files, 7 expressions, 7 same.  NOT done: the editor's diff at unpin, a GUI
item, as the linked library's note says; the unpin case checks the text
taken.

Suites green at the build: C++ 625/625 (offscreen, 8 disabled), Python 2778
OK, `SandboxProgram` 46 OK, `FeaturePythonChain` 44 OK, the view gate
(`ViewProviderHooks`, `ViewProviderChain`) 25 OK.  The cost: about 950 lines of
tests and fixtures (`SandboxProgram` +540, the fixtures module 256, the
gtests +150), 6 lines of feature code (the fix), 24 in the rig and its
driver, 23 of CMake and the 296-line tutorial, against 600-750 sized for
the D3 rows -- the overrun is the fixture-text check and the routed linked,
pinned and tamper cases, which the sizing counted as one gate.
Code: 8a0d5c30b4 (the fix), 0cc8c7ad79 (the rig), 1bae4bdf25 (D3).

**D4, BUILT 2026-09-14.**  A memory ceiling per guest, and the reset
retention measured to its cause.  Neither mechanism the sizing named was
the one: pyodide's `MAXIMUM_MEMORY` is the MAIN module's link flag, and
the main module is the pinned distribution (sec 5.3), not our wheel; its
memory is EXPORTED by the module with a 4 GB maximum compiled in
(`getHeapMax`), so no constructor clamp reaches it either.  And V8's
`ResourceConstraints` bound the engine's own heap only, where the Python
heap is not.

Measured first, on this box, a guest at default settings:

    at boot (memoryInfo())         linear 49 MB, engine heap 35-38 MB,
                                   array buffers 37 MB
    RSS after reset, no trim       316 -> 433 -> 492 -> 465 -> 482 MB
    glibc in use after each reset  7-8 MB (the boot peak is ~80 MB)
    glibc arena free after reset   166 -> 290 -> ... -> 440 MB (ten)
    M_MMAP_THRESHOLD fixed 256 KB  post-trim RSS 155-185 MB, flat (six)
    M_MMAP_THRESHOLD fixed 2 MB    post-trim RSS 152-179 MB, flat (six)
    malloc_trim after dispose      RSS after reset 236 -> 303 -> 367 ->
      (built)                      407 -> 417 MB

The retention is not a leak and not V8's heap: glibc's in-use total is
back to 7-8 MB after every reset.  What stays is free memory in the main
arena and in one arena per V8 worker thread (`malloc_info`: 18 heaps, 16
of them ~26 MB each after three resets): each time glibc frees a large
mmapped chunk it raises its mmap threshold, so the next boot's buffers
and compile zones land in arenas that do not shrink.  A fixed threshold
ends it; that is process-wide, and RULED 2026-09-14 to leave it: what is
built is `malloc_trim(0)` after a guest's isolate is disposed.

The ceiling, three layers, because the guest reaches the JavaScript
realm through pyodide's own `js` module (probed: `js.WebAssembly`,
`js.ArrayBuffer`, `js.eval` all answer):

1. *Linear memory*, `Sandbox:MemoryMB` (1024; 0 = the engine's 4 GB).
   emscripten grows the heap only through JavaScript, `growMemory` ->
   `WebAssembly.Memory.prototype.grow`, and host_shim.js replaces that
   method (and the `WebAssembly.Memory` constructor, for a memory's
   initial size) with one that asks the host's `memoryGrow` native
   first.  A refusal throws, emscripten's `growMemory` returns failure,
   sbrk fails, and Python raises `MemoryError` -- the ordinary error
   path, the guest as whole as after any exception.  The runtime
   reports `Outcome::MemoryRefused`; ImageHost rewrites a `MemoryError`
   reply to "exceeded its N MB memory budget (refused)" and KEEPS the
   guest.  Its memory stays at the high-water mark (wasm memory never
   shrinks), reused by the next evaluations and returned only by a
   reset.  The limit is read at every growth, so a preference change
   applies at once.  After each outer trip the runtime compares
   pyodide's memory with the largest size the host allowed; larger
   means it grew from wasm without asking, and the guest is dropped.
2. *The engine heap*, `Sandbox:EngineHeapMB` (512; 0 = V8's default),
   `ConfigureDefaultsFromHeapSize` at boot.  It cannot refuse: at its
   limit V8 ends the PROCESS unless a near-heap-limit callback raises
   it.  The callback terminates the guest and lends 32 MB (at most four
   times) for the termination to unwind; `Outcome::MemoryExhausted`,
   dropped like the hard stage, "exceeded its memory budget (...;
   stopped)".
3. *Array buffers*, the same `MemoryMB` over the live total, through a
   counting `ArrayBuffer::Allocator`.  A refusal is JavaScript's
   RangeError (`JsException` in Python), the guest kept.  The natives
   allocate with `ArrayBuffer::MaybeNew`: a refused `ArrayBuffer::New`
   is V8's fatal out-of-memory.

Found on the way: *V8 calls the near-heap-limit callback from the
last-resort GC that follows a failed buffer allocation*, with the engine
heap nowhere near its limit (38 of 512 MB) -- the first build stopped
and dropped the guest on a refused `js.ArrayBuffer.new`.  FIXED: the
allocator flags its refusal, and the callback leaves the limit alone
when the flag is set AND the heap is under three quarters of it, so a
stale flag cannot hide a full heap.  Sec 12.  And a JavaScript trap that
cost one boot: the replacement constructor written as `function
Memory(...)` sees ITSELF under that name, so `Reflect.construct(Memory,
...)` recursed until the stack ran out; the native is `NativeMemory`.

`FreeCAD.ExpressionSandbox.memoryInfo()` (`ImageHost::memoryInfo()`):
`live`, `linear`, `linear_limit`, `engine_heap_used`,
`engine_heap_limit`, `buffers`, `refusals`.  gtests,
`ExpressionImageMemoryTest` (6, pyodide only, MemoryMB and EngineHeapMB
at 256 and a reset so the heap limit applies): a 400 MB `bytearray`
refused with the budget message, the refusal counted, linear memory
under the ceiling and the guest's mark kept; a runaway list of 1 MB
buffers refused and 64 MB allocatable again afterwards; a 300 MB
`js.ArrayBuffer` refused as `JsException`, counted, guest kept; a
`js.WebAssembly.Memory` of 8000 pages refused at construction; a
`js.eval` loop filling the engine heap stopped, the guest dropped and
the next evaluation on a fresh one; `memoryInfo()` of a live guest and
of none.  All 94 `ExpressionImage*` / `ExpressionRouting*` gtests OK,
`SandboxProgram` 46 OK, `FeaturePythonChain` 44 OK.

Suites green at the build: C++ 633/633 (offscreen), Python 2778 OK, the
view gate (`ViewProviderHooks`, `ViewProviderChain`) 25 OK.

**The compile gate, BUILT 2026-09-14** (the gap as first built, closed the
same day on request).  Refusing wasm compilation outright after boot is
out: pyodide compiles modules at runtime -- its function-pointer wrappers
(`new WebAssembly.Module` on a generated type section), its call
trampoline, and `dlopen`'s side modules.  Every one of those IMPORTS a
memory or has none; the only module defining one is pyodide's main
module, compiled at boot.  So host_shim.js wraps `WebAssembly.Module`,
`compile`, `instantiate` and the two streaming forms, and the glue seals
it as boot ends (`__fcx_sealWasm`, deleted after the call; a shim without
it fails the boot).  After the seal a module whose memory section (id 5)
has entries is refused -- the walk is the format's own framing, a section
id byte and a u32 LEB size, so a section the engine accepts is a section
seen -- and so is every `new WebAssembly.Memory`; a stream, whose bytes
cannot be looked at first, is refused outright.  A refusal is a
RangeError (`JsException` in Python), counted in `refusals`, the guest
kept.  The guest reaches this realm, so the shim takes every intrinsic it
uses before guest code runs (`Reflect.apply` and `construct`, the typed
array, DataView and buffer slot getters, `Promise.reject`), reads the
bytes through the slots into a copy that is both checked and compiled,
and converts an argument once.  Three holes that closed, two of them in
the ceiling as first built: `grow.call(...)` and `Reflect.construct(...)`
looked up at call time would have handed a replacement the native
`grow` or constructor, and `this.buffer.byteLength` could lie to the
grow check about a memory's size; a lying `length` getter would have
hidden a section from the walk.  A compiled Module passed to
`instantiate` is told from bytes by its slots, not its prototype.
gtests (4 more, `ExpressionImageMemoryTest` 10): a memory section behind
a custom section refused, counted, the guest kept; a module importing its
memory, with an empty memory section, compiling and `Module.imports`
answering; a one-page JavaScript memory after boot refused;
`Reflect.construct` replaced and the typed array `length` getter lying,
the gate still shut.  Suites green at the gate's build: C++ 637/637,
Python 2778 OK, `SandboxProgram` 46 OK, `FeaturePythonChain` 44 OK, the
image and routing gtests 98 OK, the full sandbox GUI gate list 74 OK, and
`SandboxInitGui` with `FCX_INITGUI_IN_GUEST=1` 7 OK -- Draft's and BIM's
modules loading into the sealed guest.

NOT covered, stated: a module the guest compiles through `js` that
IMPORTS pyodide's memory can grow it with wasm's `memory.grow`, found by
the post-trip check and bounded until then by the time budget (sec 13);
a ceiling below what the boot
needs fails the boot with a log line naming the preference, untested;
the WASI reference runtime (frozen, not built here) has no ceiling.
The cost: about 470 lines of feature code and comment (the runtime
+250, the shim +42, ImageHost +78, the interface +55, the binding +44)
and 147 of tests, against 200-400 sized.
Code: 81ee7762d9 (the ceiling), 95a4344d04 (the compile gate).

**Stages.**

    D1  function objects in the image (ExpressionPy into
        ImageSources.cmake, the two throws removed, the FUNC value
        path); the surface annotations; the version stamp and
        surfaceVersion().  Gate: the gtest; the flange as an
        expression routed = native.  **BUILT 2026-09-13**, above; a
        function as the FINAL value of a routed evaluation still does
        not cross, which is the sheet carrier's routed case (P3).
    D2  the chain's recompute -- **BUILT 2026-09-13**, ProxyChain.md
        4.6: the generation counter as the cheap gate, then a
        comparison of each chain element's resolved callable against
        the one last executed, so a method edited on a linked object
        rebuilds its instances and an UNRELATED cell of the same sheet
        does not.  Design: ProxyChain.md 4.5; the gate that the sheet
        edit actually has to pass turned out to be `skipRecompute()`,
        not `mustExecute()` alone.  All eight cases green with
        OptimizeRecompute ON, the plain spreadsheet regression (an
        unrelated cell touched leaves other cells' consumers alone)
        included.
        Then App::ExpressionLibrary, the registry and lib.source /
        lib.drop, the dependency edge, the principal hash, the
        native twin, libraries() -- **BUILT 2026-09-13** for a
        library of the importer's own document, above ("D2's
        library half"): lib.drop became the "ld" field, the registry
        is named on the eval request, library functions resolve
        their module, the edge is transitive.  The linked
        library -- Source, Pinned, Snapshot, the edge through the
        links -- **BUILT 2026-09-13**, above.  P3 -- **BUILT
        2026-09-13**, above; the cross-file write RULED to run as the
        feature's file, not to prompt.  D3 BUILT 2026-09-14.  Gate:
        SandboxProgram's library half, the cross-file pair included.  The library is
        no longer the only carrier (the sheet is one today), so its
        own value is the module namespace, the import statement and
        the pinned snapshot -- and P3 of ProxyChain.md rides here:
        the chain's sheet cases re-run routed, the audit line naming
        the linked object's document, a cross-file link's write
        prompting doc.foreign once.
    D3  the five fixtures, SandboxProgram complete, the corpus list,
        the bench, the tutorial text in docs.  **BUILT 2026-09-14**,
        above; NEXT: D4.
    D4  memory: a ceiling per guest (V8's ResourceConstraints on the
        isolate, pyodide's MAXIMUM_MEMORY at wheel build) with the
        outcome a budget-style refusal, and the reset retention
        measured to its cause (V8 heap not shrinking, or the guest's
        buffers held by the host).  **BUILT 2026-09-14**, "D4, BUILT"
        above; the cause was neither -- glibc's arenas.

**Cost** (lines, new or changed):

    D1  ExpressionPy in the image, the two throws, the FUNC path       100-200
        annotations (14 + 3 XML lines, 13 table entries)                  ~40
        the version: generator, both .inc, surfaceVersion(), the
          library and document records, the open-time log            150-250
    D2  the chain's recompute (mustExecute and skipRecompute, the
          generation and identities stored at execute) and its
          eight gate cases                        BUILT: 130 + 250 test
        the object (App + Gui registration, icon)                    100-150
        the guest registry and module build                          100-150
        lib.source / lib.drop, the host side                         100-150
        the dependency edge (ImportStatement, FromStatement)           60-100
        the principal hash                                                ~20
        the native twin                                                80-120
        libraries(), Python                                               ~40
        the linked library: Source, Pinned, Snapshot, the cross-link
          edge, the relabel, the missing-file report                  150-250
        P3: the chain's sheet cases routed, the audit line            80-150
          BUILT: 550 + 385 test
    D3  the three programs, twice each, plus the sheet flange           ~200
        SandboxProgram, the gtest, the corpus list                    400-550
    D4  the ceiling and the measurement                               200-400
    total                                                             1.9-2.8k

At sec 7's pace: D1 one session, D2 two, D3 one, D4 one.

**Limits, stated** (and the re-sizing adds two, marked).  The budget
interrupts the GUEST; an OCCT call
the guest asked for runs on the host to completion, so a fillet that
takes a minute takes a minute -- the cost side of "the host always
recomputes" (1.2), and the multi-process direction's to bound.
Routed-only under enforcement, by design.  A library in another
file is reached by link, live or pinned, never by name alone (the
module name is the consumer's; the file is the link's).  No classes
in the language.  A program sees the document
through the pack as an expression does: its own object's properties
and what the identifiers resolve; it does not create document
objects (the catalog has no such row, and 1.5's prior art has no
such need).  The shared guest means one document's slow program
delays another's evaluation -- the same queue expressions share
today.  **New:** a method on a linked object WRITES its result into
the feature and the engine warns it cannot track the dependency
(`Expression.cpp:1303`), so a method that reads an object the
expression engine was never told about goes stale silently -- the
expression-on-`Shape` form has no such hole, and that is the reason
to keep both forms in the example set rather than retiring one.
**New:** a `ProxyExp` link is a real dependency (Global scope), so a
sheet that is a feature's type must not read that feature back in a
cell.  Probed: the sheet's in- and out-lists both become `[F]`, the
document logs "The graph must be a DAG" (`Document.cpp:4077`), and
the feature is left permanently Touched -- the cell still evaluates
and the sheet still reports Up-to-date, so the failure is quiet
where it matters.  `ViewProxyExp` has Hidden scope and the same cell
is fine there (no edge either way, both objects Up-to-date), which
is what that scope was chosen for (ProxyChain.md 4.4).  A type that
wants to see its instance reads it through the method's `obj`, which
is what every chain element is handed.


### 7.18 The tool bar mirror sized: the desktop's tool bars as models, streamed **[sized 2026-09-10; BUILT 2026-09-10]**

Asked by the ThinClient session (docs/ThinClient.md 8.11, its order's
item 4: "SecurePython merged; toolbars streamed; client show/hide") and
ruled 2026-09-10 (sec 11).  Three pieces: **(a)** a multi-subscriber
signal on `Gui::Fw::Store` beside its one sink, **(b)** a mirror of the
native tool bars into `QAction` / `QToolBar` models, **(c)** a JSON dump
of the model classes for their DOM backend.  (a) and (c) are small and
sized inline; (b) is the section.  The ten requirements the ThinClient
session sent on 2026-09-10 (per-connection subscription, plain JSON on
its control channel, command-name identity and order, icons by name
fetched once, action groups with the desktop's default-switching,
tooltip/status tip/shortcut text and coalesced live state, tool bar
name/title/visible and `setState` as one diff, the server's locale,
clicks as its `command` op, tool bars only) are taken as the spec and
answered one by one at the end.

**What exists, and what the mirror reuses.**

- The models: `Fw::QAction` (text, icon as a PATH or name string,
  checkable, checked, shortcut, separator, `command` / `commandIndex`)
  and `Fw::QToolBar` (a `Layout::Bar` of action refs, separators and
  widgets; iconSize, toolButtonStyle, movable, floatable, orientation,
  toggleViewAction), both from 7.15, with `Widget`'s own enabled,
  visible, toolTip, statusTip, objectName, windowTitle.
  `FwQt::realizeAction` binds a model whose `command` is set to the
  host command's REAL `QAction` (`commandAction`: `Command::getAction()`,
  `initAction()` first, the group's member by `commandIndex`, 1-based,
  0 the group's own) through an `ActionView` that reads the real action
  back and applies the model's touched keys -- so a model already
  stands for a real action, in the model -> action direction.
- The store (`FwStore.h`): objects by comm id, guest-driven `commOpen`
  / `commUpdate` / `commCustom` / `commClose`, one `Sink`; `watch`
  forwards a `propertiesChanged` not from `Source::Guest` as an
  `update` of the `q_` keys and an `eventEmitted` as a `custom`.
  `Layout::spec()` already serializes a layout tree (items: widget /
  layout / action refs as `QObject*`, separator, stretch, spacing,
  spacer, pos).
- The desktop: `ToolBarManager::toolBars()` (name -> `QToolBar`, the
  main window's, the status bar's and the menu bar areas'),
  `setup(ToolBarItem*)` rebuilding the bars on a workbench switch,
  `setState(names, state)` on a sketch edit (visibility and the toggle
  action only; the bar set does not change), `toolbarNames` the
  workbench's declared order.  Action state: `MainWindow::updateActions`
  -> `CommandManager::testActive` on a 150 ms timer and on selection
  changes; `QAction::changed` fires only when a value actually changes.
  A group: `ActionGroup` (a `QActionGroup` of the members, separators
  included, `isExclusive`, the `_dropDown` face), the group's own
  `QAction` re-faced from the member at `property("defaultAction")`
  (0-based over `actions()`); a click on a member goes
  `ActionGroup::onActivated(QAction*)` -> `Command::invoke(index,
  TriggerChildAction)` -> `onInvoke` sets `defaultAction` and re-runs
  `setup`, and `GroupCommand::activated` persists it.  A member's
  `Action` parent names its `Command` (`Action::command()`); the members
  of a `RecentFilesAction` or `WindowAction` are plain `QAction`s with
  no command.
- The channel: `registerSceneControlOp(op, mutating, handler(req,
  boundDoc))` -- the registered form has NO client id, the built-in ops
  (`edit`, `command`, `onViewFocus`) take one; `SceneStreamServer::
  sendControl(client, json)` queues a text frame to one connection,
  `setClientClosedHandler` says when one leaves; `runCommandOp` is
  `runCommandByName` under the client's `ViewerScope`, allowlisted to
  `Sketcher_Create*` (docs/ThinClient.md 8.7).

**(a) The fan-out signal, sized.**  On `Store`:

- `Q_SIGNAL void message(const QString& id, const QString& method,
  const QVariantMap& content, quint64 origin)`, emitted wherever the
  sink is called today (the two `watch` lambdas), whether or not a sink
  is set; `open` and `close` are emitted too (the sink never needed
  them: the guest opened the comm), so a subscriber sees the object's
  whole life.  The sink stays what it is.
- `Store::OriginScope` -- an RAII tag (`quint64`, 0 the desktop) the
  stream sets while it applies a client's write, carried on every
  `message` the write causes: the fan-out skips the writer, which is
  the layer's "a `Source::Backend` write does not echo to its writer"
  rule extended from one writer to N.
- `Store::snapshot(id)` -> `{model, qtClass, state (the q_ keys),
  layout (Layout::spec with the refs as IPY_MODEL_ strings), parent
  (ref or empty)}`, what a late subscriber gets per object, in an order
  where a referenced object precedes the referrer.
- `Store::adopt(id, Widget*)`: a host producer's object under a
  synthetic id, watched like a comm's; `commClose` of an adopted id
  from the guest is refused (`false`), `reset()` keeps them (a guest
  reset is not the desktop's).  `Store::notifyLayout(id)`: an outbound
  `update` carrying `layoutSpec` (today only `q_` keys go out, layouts
  being the guest's own).
- Applying a client's write: `Store::applyUpdate(id, state, origin)`
  = `commUpdate` from `Source::Backend` under the scope, and
  `applyCustom(id, content, origin)` the same for a request.

Gtests in `tests/src/Gui/FormWidgets.cpp`: two slots on `message`, a
Backend write under origin 7 arrives at both with origin 7; a snapshot
of a bar with an action, a separator and a widget round-trips as refs;
an adopted object survives `reset()` and refuses a guest close.  About
150 lines of C++ and 80 of test.

**(b) The mirror, sized.**

*Identity.*  Every mirrored object is a store object under a synthetic
id, keyed as the client keys: `cmd:<Command name>` for a command's
action, `cmd:<group>#<k>` for the member `k` of a group (k =
`commandIndex`, 1-based over `ActionGroup::actions()`, separators
counted, so the id names the SAME real `QAction` `commandAction` would
return), `toolbar:<objectName>` for a bar (the `ToolBarManager` key),
`widget:<bar>#<n>` for a widget action, and ONE list object,
`toolbars`, whose layout is the ordered bars.  Ids are stable across
snapshots and workbench switches: a command's real `QAction` lives as
long as the command, and so does its model.

*The models.*  A `Fw::QAction` per real action, bound the existing way
-- `command` / `commandIndex` set, `FwQt::realizeAction` making the
`ActionView` onto the real action (a write from a client then goes
model -> real action exactly as a guest's would) -- and kept CURRENT
the new way: the mirror connects the real action's `changed` and
refreshes the bag from it (`Source::Native`: text, icon, toolTip,
statusTip, shortcut, enabled, visible, checkable, checked).  The read
back writes only what differs, so a client's own write, having reached
the real action, comes back as no change and no message.  Four traits
added to `QAction` on both sides (C++ model and `models.py`, the guest
never sets them): `members` (refs, in `actions()` order, a separator a
member with `separator` true), `defaultAction` (int, 0-based over
`members`, -1 none -- the desktop's `property("defaultAction")`, read
after every `onInvoke`), `exclusive` (bool), `dropDown` (bool).  A
member model: `command` = the GROUP's name, `commandIndex` = k (its
binding identity), plus `memberCommand` = the member's own command
name when it has one, else "" (a plain-`QAction` member of a
`RecentFilesAction`).  Icons: the `icon` trait carries the NAME --
`Command::getPixmap()` for a command action, which is what
`BitmapFactory().iconFromTheme` resolves, a theme name or a file
path as the workbench wrote it; for the group's own action the default
member's name; for an action with an icon and no name (a toggle
action, a plain member) "" -- the client shows text.  Never pixels.
Tooltip and status tip as the desktop shows them (the `QAction`'s,
translated by `Command::setup`; the rich tool tip `Action::
createToolTip` makes is HTML -- the mirror sends the plain
`Command::getToolTipText` translation, the HTML form is the desktop's
own composition of that text, the title and the shortcut); the
shortcut as `QKeySequence::toString(NativeText)`.

A `Fw::QToolBar` per bar: `objectName` = the name, `windowTitle` = the
translated title, `visible` (the bar's `isVisibleTo(mainWindow)`, so
`setState`'s hide reads as `visible: false`), `orientation`,
`iconSize`, `toolButtonStyle`, and one new trait `area` (string:
`top` / `left` / `right` / `bottom` / `statusbar` / `menubar-left` /
`menubar-right` -- the fork parks bars in the status bar and beside
the menu bar); its `bar()` layout is the real bar's `actions()` in
order: a command action as its `cmd:` ref, a separator as a separator
item, a `QWidgetAction` (the workbench combo, a spin box) as a
`widget:` ref to a plain `Fw::Widget` whose `qtClass` is the real
widget's class name -- the kind a client may skip.  The `toolbars`
object (a `Fw::Widget`, qtClass `QMainWindow`) has a bar layout whose
widget items are the `toolbar:` refs in the desktop's order: by area,
then by geometry (row, then x) for shown bars and by `toolbarNames`
(the declared order) for hidden ones -- the visual order after the
user drags a bar is `QMainWindowLayout`'s private state, and geometry
is its only public trace.

*The producer.*  `Gui::Fw::ToolBarMirror` (`src/Gui/Fw/
FwToolBarMirror.h/.cpp`), a singleton `QObject` started by the first
subscriber and stopped by the last (a desktop with no client runs
nothing; `stop` closes the adopted objects).  `rebuild()`: walk
`ToolBarManager::toolBars()`, make or update the models, diff against
the previous set -- bars gone are closed, new ones opened, a bar whose
content changed gets `notifyLayout`, and the `toolbars` list is
re-sent ONCE (its `layoutSpec`).  That is the "one snapshot-level
diff": bar names are the client's keys, action ids do not change, so a
workbench switch is per-bar opens and closes plus one order message,
never per-action churn.  Triggers: a new signal `ToolBarManager::
toolBarsChanged()` emitted at the end of `setup(ToolBarItem*)` and of
`setState()`; `Application::signalActivateWorkbench`; each bar's
`visibilityChanged` (a bar-level update); an event filter per bar for
`QEvent::ActionAdded` / `ActionRemoved` (Draft's tray, a workbench
adding to a bar at run time -- that bar alone is re-laid).  Live
state: `changed` on every mirrored real action marks it dirty; a 0 ms
single-shot flushes the dirty set as one `setProperties` per action
(one `update` message, the changed keys only).  `testActive`'s 150 ms
sweep touches every command but `changed` fires only on a real
change, so a sweep that changes nothing sends nothing, and one that
enables twenty commands sends twenty single-key updates in one tick.
A group's `defaultAction` is read back on the group's own `changed`
(re-facing changes its icon and text, which is the same tick).

*The transport* -- `src/Gui/SceneWidgets.cpp`, in Gui, on the control
channel of docs/ThinClient.md 4.2, one op per store message:

- The registry gains a client-aware form: `registerSceneControlOp(op,
  mutating, handler(req, boundDoc, client))` beside the existing one
  (the `RegisteredOp` struct carries either; `handleSceneControlRequest`
  already has the client id).
- `{"op":"widgets.subscribe", "toolbars": true}` -- the connection
  joins the tool bar stream (`"all": true` joins everything in the
  store: the guest's forms, later the task panel); the reply is `ok`
  plus `"theme"` (the desktop's icon override set, `MainWindow::
  overrideIcons`, "" for the stock theme); then, from the GUI thread,
  one `{"op":"widgets","method":"open","id":..,"model":..,"qtClass":..,
  "state":{..},"layout":{..},"parent":..}` per existing object in
  reference order, and from then on `{"op":"widgets","method":
  "update"|"custom"|"close","id":..,"content":{..}}` for every store
  `message` whose origin is not this client.  `{"op":"widgets.
  subscribe","toolbars":false}` or `widgets.unsubscribe` leaves; a
  closed connection leaves through `setClientClosedHandler`; the last
  one out stops the mirror.  Subscription is per connection: a phone
  that says nothing gets nothing.
- `{"op":"widgets.icon","name":..,"size":24}` -> `{ok, "name",
  "format": "svg", "data": "<svg ..."}` (the SVG text itself) when the
  name resolves to an SVG file (a small new `BitmapFactory().
  iconSource(name)`: the file `iconFromTheme` would load, read as
  bytes -- the theme override consulted, the same lookup), else
  `{"format": "png", "data": <base64>, "size": n}` from
  `QIcon::pixmap(n)` where only a pixmap exists (a PNG resource, a
  Python command's bitmap); `{ok:false, code:"UnknownIcon"}` otherwise.
  The `format` field is the ThinClient session's ask of 2026-09-10:
  the channel is JSON text, so the encoding is stated, never sniffed
  (its reply also accepted the two deviations below).  The client
  caches by (theme, name); a theme change on the desktop is one
  `custom {"event":"theme","args":[name]}` on the `toolbars` object,
  which is the cache-drop cue.
- `{"op":"widgets.update","id":..,"state":{"q_checked":true}}` ->
  `Store::applyUpdate` under the client's origin -- the model's
  `ActionView` writes the real action; `{"op":"widgets.custom","id":..,
  "content":{"event":"trigger"}}` -> `applyCustom` -> the model's
  `request("trigger")` -> the real `QAction::trigger()`.  For a member
  that is the real `QActionGroup`'s `triggered`, so `ActionGroup::
  onActivated(QAction*)` runs, `invoke(index, TriggerChildAction)`
  moves the default and the group's face, and the next tick shows it
  -- the desktop's own path, not a re-implementation.  Both ops are
  `mutating` (refused on a view-only connection).
- The `command` op gains an optional `"index"` (1-based, the model's
  `commandIndex`): `cmd->invoke(index - 1, Command::TriggerChildAction)`
  -- the same member path for a client that keys on command names and
  never opens the model (the ThinClient session's item 9); without
  `index` it is `runCommandByName` as today.  The allowlist stands as
  theirs: a click on a command it refuses reads `CommandRefused`, which
  that session accepts until dialogs mirror (8.11).
- Values on the wire are the bag's, which is traitlets-shaped already:
  strings, bools, ints, floats, lists, maps, refs as `IPY_MODEL_<id>`
  strings, a colour as `[r, g, b, a]` floats (the layer's `q_color`),
  an enum as its Qt integer.  `QVariant` -> `QJsonValue` is the whole
  conversion; nothing `QVariant`-only crosses.

*The gate.*  `SandboxToolBarMirror`, a GUI gate module in the rig's
default list (`scripts/sandbox-gui-gate.py`), driven through a
`FreeCADGui.FormWidgets` hook (`mirrorToolBars(on)`, `snapshot(id)`,
`messages()` -- a drained list of the store's `message` emissions, the
gate's subscriber): activate Draft, assert the `toolbars` order names
Draft's bars in area order, each bar's layout its actions as `cmd:`
refs with the kinds right (a separator, the workbench combo as a
`widget:` item); a group command (`Draft_...` has none natively --
`Std_ViewIsometric`'s `Std_ViewGroup` or `Part_CompPrimitives`): its
members, the default, `invoke(k, TriggerChildAction)` from the test
moving `defaultAction` and re-facing the group's `icon` in the next
tick; a checkable (`Std_ToggleVisibility` is not; `Draft_ToggleGrid`
is) toggled from the model reaching the real action and coming back
as no message; `testActive` with a selection change producing one
update per changed action and none otherwise; a sketch edit
(`Std_SketchEdit` needs a document: the gate opens a sketch, calls
`Gui.ActiveDocument.setEdit`) arriving as bar-level `visible` updates
and one order message, no action churn; switching to BIM closing
Draft's bars and opening BIM's with the `cmd:` ids of shared commands
unchanged.  The transport in Tests_run (`tests/src/Gui/
SceneWidgets.cpp`): the stream with an injected sender in place of
`SceneStreamServer::sendControl`, subscribe / snapshot order /
update / unsubscribe / a closed client, and the `widgets.icon` op on
a stock SVG and a PNG.

*Budget.*  About 1100 lines of host C++ (the mirror 550, the stream
and the three ops 300, the store's (a) 150, the registry form, the
manager's signal, the `command` op's index and the icon source 100),
15 lines of `models.py` (the five traits), and 400 of gate.  One
session, (a) first because the gate needs it.

**(c) The model dump, sized.**  `src/Tools/bindings/dumpWidgetModels.py`
reads `freecad/widgets/models.py` and `items.py` with `ast` -- not by
importing them: the conda Python has no `ipywidgets` or `traitlets`,
and the guest's is in pyodide -- and writes JSON: per class with a
`_model_name` (its Python name, `qt_class`, the base model, the `q_`
traits with trait type as a JSON type name -- Unicode `string`, Bool
`bool`, Int `int`, Float `float`, List `list` with its item type, Dict
`object` -- the default literal and `allow_none`), the `Signal(...)`
declarations with their argument specs, and the `CLASSES` map (Qt
class name -> model).  Shape: `{"module": "freecad.widgets",
"version": "0.1", "prefix": "q_", "classes": {"QActionModel":
{"python": "QAction", "qtClass": "QAction", "base": "QWidgetModel",
"properties": {"text": {"type": "string", "default": ""}, ...},
"signals": {"triggered": ["bool"], ...}}, ...}, "qtClasses":
{"QDialog": "QDialogModel", ...}}`.  A trait whose default is not a
literal (a call, a name) is recorded with `"default": null` and
`"opaque": true` rather than guessed.  The build runs it beside the
wheel (`src/App/CMakeLists.txt`, the `WidgetsSandboxWheel` block) into
`<build>/<datadir>/Pyodide/widget-models.json` (`share/Pyodide/` in
the conda tree, beside the wheels), installed under `<datadir>/
Pyodide/`, so the DOM backend's build reads one file with no Python of
its own; the script also runs alone.  Test: a plain
unittest in the Python suite (`src/Mod/Test/SandboxModelDump.py`, no
GUI) running the script on the tree and asserting the `QActionModel`
entry, the base chain to `QWidgetModel`, and that every `CLASSES` value
is a dumped class.  About 200 lines of Python and 40 of test.

**The ten requirements, answered.**

1. Per connection: `widgets.subscribe` / `unsubscribe`, snapshot on
   attach, diffs after, nothing to a connection that never asked.
2. Plain JSON on the control channel, one op per store message; refs
   as strings, colours as float lists (the layer's form, in the dump),
   enums as Qt integers.  Not hex strings: the dump names the type,
   and one encoding on both wires is worth more than a second one.
3. Bars in area-then-geometry order, actions in bar order, identity
   `command` + `commandIndex` (the command name, and the member index
   for a group), separators as layout items, widget actions as a
   `widget:` kind with the real class name.
4. Icons by name in the stream, `widgets.icon` fetching SVG bytes or a
   PNG at a stated size once, cached by (theme, name); a theme change
   is one event.  No per-icon overlay state: the fork's override is
   one global set, and the desktop varies nothing else per icon.
5. Groups: `members`, `defaultAction`, `exclusive`, `dropDown` on the
   group's model; a member's trigger goes through the real
   `QActionGroup`, so the default moves as it does for the desktop
   user and the next tick shows it; checkable and exclusive members
   read as `checkable` / `checked` live.
6. toolTip, statusTip, shortcut text, enabled / visible / checked live,
   coalesced per 0 ms tick; `testActive` costs one message per action
   that actually changed.
7. Per bar name, title, `visible`, `area`, the ordered layout;
   `setState` arrives as bar-level `visible` updates and one order
   message, the action ids untouched.
8. The server's locale, noted in the subscribe reply (`"locale"`).
9. The `command` op as today, plus `index` for a group member; the
   model path (`widgets.custom` trigger) is the alternative for a
   client that holds the models anyway.
10. Tool bars only: no menu bar, no context menus, no dock or status
    bar, no task panel -- the latter being 7.12's port, and it will
    ride the same stream when it exists.

Nothing in the ten enlarges the sizing.  The two things NOT done as
asked, said plainly: colours stay float lists (2), and the visual order
of hidden bars is the declared order (3).

**Built 2026-09-10, as sized, with these notes.**

- **(a)** `Fw::Store::message(id, method, content, origin)` beside the
  sink; `adopt` / `release` / `isAdopted`, `snapshot`, `snapshotOrder`,
  `applyUpdate` / `applyCustom`, `notifyLayout`, `OriginScope` /
  `currentOrigin`; `open` and `close` announced for comm objects too;
  `reset()` keeps the adopted objects and announces the guest's
  closes.  `snapshotOrder` follows an object's state and layout refs
  but NOT its parent ref: a container names its children through its
  layout, so following the parent back put the container before its
  last child (the first gate run showed `toolbar:Clipboard` after
  `toolbars`).  A write from a client is echoed as the layer has it --
  an equal write is still a write -- under the client's origin, which
  the stream skips; nothing comes back from the real action for an
  equal value (the mirror writes only what differs).
- **(b)** `Gui::Fw::ToolBarMirror` (`src/Gui/Fw/FwToolBarMirror.*`),
  `Gui::SceneWidgetStream` and the five `widgets.*` ops
  (`src/Gui/SceneWidgets.*`, registered by
  `installSceneControlHandler`), the client-aware
  `registerSceneControlOp` form, `ToolBarManager::toolBarsChanged`
  (and `toolBars()` made public), `ActionGroup::hasDropDownMenu`,
  `BitmapFactory::iconSource(name, size, format)`, the `command` op's
  `index`, the five `QAction` traits and `QToolBar.area` on both sides.
  One rule the sizing did not state: a bar whose toggle action the
  desktop hides -- another workbench's, or one `setState` forced
  hidden -- is NOT mirrored (`ToolBarManager::toolBars()` lists every
  bar ever made, hidden ones included; the first gate run mirrored
  Part Design's bars under Draft).  So a workbench switch IS per-bar
  closes and opens plus one order message, and a `setState` hide is a
  close, which is item 7 as asked.  A client's write to a mirrored
  action goes through `FwQt::bindAction` (bound by pointer), not the
  command write filter: the filter is for the guest.  A connection
  that is gone is dropped when a push to it fails (the server's closed
  handler is per document group; the stream is not).  The tool tip is
  the command's translated tool tip text; a plain action's rich text
  is flattened.  Not built: the theme-change event on the `toolbars`
  object (a theme change is a stylesheet change; a client
  re-subscribes) -- sec 13.  The `command` op's `index` sits behind
  that op's `Sketcher_Create*` allowlist (docs/ThinClient.md 8.7) like
  the rest of it; the model path (`widgets.custom` with `trigger` on
  the member) is the one the gate exercises, and it moves the default
  the desktop's way.
- **(c)** `src/Tools/bindings/dumpWidgetModels.py`, the
  `WidgetModelsJson` target writing `<build>/share/Pyodide/
  widget-models.json` (39 classes, 54 Qt names), a base that is no
  model of its own (`QAbstractItemView`, `QAbstractButton`,
  `QAbstractSpinBox`, `InputField`) folded into its model subclasses.
- **Gates**: `FormWidgets.cpp` `test_storeFanOut` and
  `test_widgetStream` (19 cases now); `SandboxToolBarMirror` (5, in
  the GUI gate's default list, last, no guest: the structure under
  Draft, `Std_DrawStyle` as the group, the coalescing on a selection
  change, the transport with the injected sender, the switch to Part);
  `SandboxModelDump` (6, in the Python suite).  Hooks for the gates on
  `FreeCADGui.FormWidgets`: `watchMessages` / `messages`, `snapshot`,
  `snapshotOrder`, `mirrorToolBars`, `mirrorFlush`, `control` (a
  request under a client id, the stream's sender injected: a client
  above 1000 is "gone") and `pushed`.
- **Facts that bit**: this fork's Draft bars are "Draft Creation",
  "Draft Modification" (not upstream's "Draft creation tools");
  `FormWidgets`' `variantToPy` flattens a nested list, so `pushed()`
  returns maps; under `FreeCADCmd` `sys.executable` is FreeCAD, so the
  dump test finds a plain interpreter under `sys.base_prefix`.

### 7.19 The panel mirror sized: the desktop's task panel as models, streamed **[sized 2026-09-10; M1 BUILT 2026-09-10; M2 BUILT 2026-09-10; M3 BUILT 2026-09-11; M4 MEASURED 2026-09-11]**

Asked 2026-09-10, after the question "do we need to modify external
Python workbench code to hook their task panels to our widget
protocol": the shim route (a workbench's `PySide` import resolved to
the model classes, sec 11 item 5) needs no edit for the bulk of a
workbench and an edit per residue file (the linter over CAM: 438
files, 139 using PySide, 30 with residue in 74 uses -- event filters,
`QFileDialog`, drag and drop, `QSvgRenderer`, rich text,
`QApplication` handles), and it never reaches a C++ panel at all.
The alternative RULED 2026-09-10 ("B is good"): leave the workbench
on real Qt, unmodified, and MIRROR the realized task panel -- the
tool bar mirror of 7.18 generalized from `QToolBar` and `QAction` to
the widget tree under `Control().showDialog`.  The workbench's event
filters, drag and drop, timers and Coin trackers keep running on the
host, where the real widget is; the browser sees a reflection and
writes back through the store.  This covers what the shim cannot: the
93 C++ `TaskDialog` subclasses of 7.12's corpus reach the browser
without the H2 and H3 ports.  What it does not cover is the tier with
no Qt: a serving desktop is the host, as in the thin-client model
(docs/ThinClient.md 8.11, the shared session); the pure WASM tier
keeps the shim route.  It does not touch 7.16 (the guest's Coin scene
into the host's, the other direction, dropped in the re-aim) -- the
two share only the pattern 7.16 arrived at, a walk of real objects.

**What exists, and what the mirror reuses.**  More than 7.18 had:

- **The binder.**  `FwQt::View::bind(model, widget)` adopts a real
  QWidget as the rendering of a model (the UiLoader path of H0, a
  ported panel's uic'd children).  It `readBack()`s every bag key
  that is a `Q_PROPERTY` of the real widget through the meta-object
  (`QColor` to a list, enums and flags to ints, icons skipped), and
  `connectWidget()` wires the widget's edit signals into the bag as
  `Source::Backend` writes and events -- `toggled`/`clicked` on a
  button, `textEdited`/`returnPressed`/`editingFinished` on a line
  edit, `valueChanged` on the spin boxes and `InputField`,
  `currentIndexChanged` on a combo, the tab and stack `currentChanged`,
  `ColorButton::changed`, `FileChooser`, the dialog button box,
  `QuantitySpinBox` and `PrefQuantitySpinBox` by their own casts, 23
  real classes in all.  `initItems()` on a bound item view attaches
  to its REAL `QAbstractItemModel` (a `QStandardItemModel` is made
  only when there is none): `clicked`/`doubleClicked`/`activated`/
  `pressed`, `expanded`/`collapsed`, the selection and current
  changes, `dataChanged` per cell, and `readBackItems()` for the
  columns, headers and widths.  In the model -> widget direction
  `apply()` orders the keys (range before value, items before index)
  and `applyOne` calls the real setters.  So per widget the mirror is
  `createWidget(realClass)` plus `bind`; the binder is the walker's
  per-node body already, both directions.
- **The class map.**  `Fw::createWidget(className)` answers a Qt class
  name through `classTable()` (the model whose `qtClass` it is, a
  model name accepted too) and a plain `Widget` for an unknown one --
  the real widget's meta-object chain walked upward until the table
  answers is the whole mapping: `Gui::PrefCheckBox` lands on the
  `QCheckBox` model as it does for a .ui file, `Gui::UrlLabel` on
  `QLabel`, Sketcher's `ConstraintView` on `QListWidget`.
- **The store and the stream** (7.18 (a), built): `adopt(id, widget)`
  for a host producer's object, `message(id, method, content,
  origin)` for the fan-out, `snapshot(id)` and `snapshotOrder()` for a
  late subscriber, `applyUpdate`/`applyCustom` under `OriginScope` for
  a client's write; `SceneWidgets.cpp`'s ops `widgets.subscribe`
  (`toolbars`, `all`), `widgets.icon` by name, `widgets.update`,
  `widgets.custom`, and `SceneWidgetStream::wants(client, id)` which
  already passes everything to an `all` subscriber.  A client's
  `widgets.update` on a mirrored object is `commUpdate` from
  `Source::Backend` -> `View::propertiesWritten` -> `apply` -> the
  real widget's setter, which fires the real widget's signals into the
  panel's own slots: the input direction needs no new code for what
  the setters already signal (`setValue` -> `valueChanged`, `setChecked`
  -> `toggled`, `setCurrentIndex` -> `currentIndexChanged`).
- **The trigger.**  `TaskView::showDialog` emits
  `Control().signalShowDialog(TaskView*, contents)` with the dialog's
  content widgets (`TaskView.cpp:595`) and `signalRemoveDialog` on
  removal (`:675`); `ControlSingleton::closedDialog` clears the active
  dialog on `TaskDialog::aboutToBeDestroyed`.  The content is
  `TaskBox`es (`QSint::ActionGroup`: `headerText`, `expandable`,
  `header` as Q_PROPERTYs, `toggledExpansion`), a Python panel's
  `form` widgets wrapped in one each (`TaskDialogPython::appendForm`).
- **The mirror's own pattern** (7.18 (b), built, 688 lines): objects
  adopted under synthetic ids, a `QPointer` map real -> model, a 0 ms
  rebuild timer and a 0 ms flush timer coalescing the dirty set, an
  event filter on the container for structural events
  (`ActionAdded`/`ActionRemoved` there), `writeInitial`/`writeDiff`
  so only changed keys leave, `destroyed` releasing the model.
- **Key replay.**  docs/ThinClient.md 8.7 drives a parentless
  `QuantitySpinBox` with replayed key events; the same primitive
  serves the edit-finish semantics below.

**The corpus** (the linter's `--ui` over `src/Mod` and `src/Gui`, 509
.ui files, 2026-09-10; the class chain decides the model):

    class                        widgets  files   model it lands on
    ---------------------------  -------  -----   -----------------------------
    QLabel                          2480    438   QLabel
    QPushButton                      669    183   QPushButton
    QGroupBox                        579    244   QGroupBox
    QWidget (containers)             577    385   Widget, its layout walked
    Gui::QuantitySpinBox             527    108   QuantitySpinBox (bound today)
    QCheckBox                        447    178   QCheckBox
    Gui::PrefCheckBox                369     73   QCheckBox (a .ui file's rule)
    QComboBox                        365    194   QComboBox
    QLineEdit                        300    135   QLineEdit
    QToolButton, QDoubleSpinBox,     167+   ...   their own
      QRadioButton, QSpinBox
    Gui::InputField                  130     27   InputField (bound today)
    Gui::Pref* (11 classes)          ~800          the base each extends
    QListWidget, QTreeWidget,        81/44/        ItemView family (rows
      QTreeView, QTableWidget,       23/19/        reflected, below)
      QTableView, QListView          11/4
    QSlider, QFrame, Line,           38/35/70      their own; Line a QFrame
      QTextEdit, QPlainTextEdit,     24/12/7
      QTextBrowser
    QStackedWidget, QTabWidget,      19/18/        containers, walked
      QSplitter, QScrollArea         18/11
    no model of their own:           ~30    ~25   the chain, or the picture
      QToolBox 5, ActionSelector 3,
      AccelLineEdit 4, UrlLabel 4,
      MatGui::MaterialTreeWidget 6,
      MatGui::ImageLabel 2,
      QtColorPicker 2, SqueezeLabel
      2, ConstraintView, ElementView,
      EditTableView, StatefulLabel 2,
      ExpressionTextEdit 2,
      PrefCheckableGroupBox 1

Every class reaches a model or a container through its base:
`QToolBox` is a `QFrame` (its pages walked, the tab semantics lost),
`MaterialTreeWidget` and `ActionSelector` are `QWidget` composites
walked through to the real views and buttons inside them,
`ImageLabel` a `QLabel`.  Only a custom-painted leaf takes the
picture fallback.  Beyond the .ui files, the code-built widgets of
the 90 dialog units and the 109 Python `showDialog` sites use the same
classes (7.12's corpus), and Sketcher's constraint and element lists
put custom item widgets in a list (`setItemWidget`), which reflect as
their cell text -- the 7.12 outlier, unchanged.

**The design.**

- **Root and ids.**  One adopted list object `panel` (as `toolbars`
  is), a `Fw::QDialog` model `panel:<n>` per shown `TaskDialog` (`n` a
  counter; `windowTitle` from the dialog, `standardButtons` from
  `getStandardButtons()`, the button box's clicks crossing as the
  dialog's `accept`/`reject`/`clicked(id)` requests, answered by
  `Control().accept()`/`reject()`/the dialog's `clicked`), holding one
  `QGroupBox` model per `TaskBox` with `qtClass`
  `Gui::TaskView::TaskBox`, `title` = `headerText`, `checkable` =
  `expandable`, `checked` = expanded (`toggledExpansion` both ways),
  the icon by name where the box has one; under it the walked
  content.  Widgets get `pw:<n>`.  Ids are minted per instance and
  never reused, so a client that missed a `close` cannot write into
  a stranger.
- **The walk.**  Depth first from each `TaskBox`'s content widget:
  for a `QWidget` whose chain `classTable()` answers, `createWidget`
  with the real class name as `qtClass`, `View::bind`, adopt; then
  its `layout()` as a `Fw::Layout` of the same kind (`QVBoxLayout`/
  `QHBoxLayout`/`QGridLayout`/`QFormLayout` -> `VBox`/`HBox`/`Grid`/
  `Form`; `QGridLayout::getItemPosition` and
  `QFormLayout::getItemPosition` for `pos`; `QSpacerItem` ->
  `addSpacer` with its policies; stretch and spacing; nested layouts
  recursively; margins and spacing read), the layout's widgets walked
  as the layout places them; a child no layout holds (a container
  built by hand, `QScrollArea::widget()`, a tab or stack page,
  `QSplitter` children) walked as a child with its geometry in the
  bag.  A `QWidget` whose chain answers nothing and that has children
  is a container (a plain `Widget`, walked through); one with no
  children is a LEAF the walker does not understand: the picture
  fallback.  Hidden widgets are walked and sent with `visible` false
  (a stack's other pages exist for the client to switch to).
- **The watch** -- what `bind` does not do.  `bind` reads once and
  hears the user's edits; a panel then drives its own widgets from
  code (a label's text after a recompute, a group enabled by a check
  box, a combo refilled), and 39 of a `QWidget`'s 43 writable
  properties carry NO notify signal (`enabled`, `visible`, `toolTip`,
  the geometry, `QLabel::text` among them; measured through PySide's
  meta-objects, 2026-09-10).  Rather than a table of notify signals
  per class, the mirror re-reads on the widget's own evidence: an
  event filter on every mirrored widget marks it dirty on `Paint`,
  `EnabledChange`, `Show`, `Hide`, `ToolTipChange`, `FontChange`,
  `StyleChange`, `LanguageChange`, `Resize`; a 0 ms flush re-runs
  the meta-object read of the dirty widgets' bag keys and writes ONLY
  the keys whose value differs from the bag, as `Source::Backend`
  (the fan-out sends them; the binder's own `apply` is skipped by
  identity).  A widget that changed while hidden repaints on `Show`
  and is caught then, which is the right time for a mirror.  Cost:
  a task panel holds 50 to 200 widgets with about ten keys each; a
  re-read is a meta-object property read per key, microseconds per
  widget, once per repaint burst -- to be measured on
  `TaskPadParameters` and Draft's `task_orthoarray` as the first
  number (sec 8).
- **The structure watch.**  `ChildAdded`/`ChildRemoved` (on the next
  tick: at `ChildAdded` the child is a bare `QObject` still under
  construction, a known Qt trap) and `LayoutRequest` on a mirrored
  container schedule a re-walk of that subtree, diffed against the
  real -> model map: new widgets adopted, gone widgets released,
  moved ones re-placed (`notifyLayout`).  `destroyed` on any mirrored
  widget releases its model (the binder's `QPointer` goes null on its
  own).  `signalRemoveDialog` and `closedDialog` release the panel.
- **The input semantics** the setters do not give.  `setText` fires
  `textChanged`, not `textEdited` or `editingFinished`, and panels
  connect the latter two.  A client's `text` write on a line edit
  therefore goes `setText` then the `textEdited(text)` signal (public
  since Qt 5, emitted by the mirror), and a `custom` `editingFinished`
  / `returnPressed` request replays the key (Return through
  `QApplication::sendEvent`, 8.7's primitive) so the widget's own
  handling runs.  `click` on a button is `QAbstractButton::click()`
  (the existing `requested` path).  Focus requests `setFocus`.  Item
  view edits go through the real model's `setData` (the existing
  `applyItemOp`), selection through the selection model.
- **Item views, reflected.**  `initItems` today assumes the model
  owns the rows; a mirrored view's rows are the panel's.  A REFLECT
  mode of `Items`: ids minted for the real model's existing rows
  (`QPersistentModelIndex` per id), the row tree sent as one
  `snapshot()` at adoption, then `rowsInserted`/`rowsRemoved`/
  `rowsMoved`/`modelReset`/`layoutChanged` and the existing
  `dataChanged` kept as item ops.  Cells read the roles `ItemCell`
  has (text, icon by name where the decoration is a named pixmap,
  else skipped, tool tip, check state, flags, colors, font bold,
  alignment).  A `setItemWidget` cell reflects as its text.
- **The picture fallback.**  A leaf with no model becomes a `QLabel`
  model with `qtClass` the real class and a `pixmap` key holding an
  image id `img:<sha1 of the PNG>`; the client fetches it once
  through a new `widgets.image {id}` op (base64 PNG, a sibling of
  `widgets.icon`), the mirror re-grabs (`QWidget::grab()`) on the
  widget's `Paint`, coalesced with the flush, and sends a new id only
  when the bytes changed.  Quotas: a size cap per grab (the device
  pixel ratio ignored, 1x), a rate cap per widget (a blinking caret
  in a custom editor would otherwise stream at the blink rate), a
  count cap per panel.  Mouse input to a picture is a `custom`
  `mouse(type, x, y, buttons)` request replayed as a `QMouseEvent`
  -- the `EventRelay` of `watchEvents` reversed -- third stage,
  optional; a picture is display first.
- **Nested modals.**  A panel's slot may `exec()` a `QMessageBox`, a
  `QFileDialog` or its own `QDialog`; the nested loop keeps serving
  the socket, so the stream stays up, but the client sees nothing.
  Third stage: an application-wide event filter on `Show` of a
  top-level `QDialog` mirrors it as a root `dialog:<n>` by the same
  walk (a `QMessageBox`'s buttons and text are ordinary widgets; a
  non-native `QFileDialog`'s are too; the platform's native file
  dialog is not a QWidget tree and is the client's own path picker,
  the `FileChooser` model, ThinClient's side).  Under the shared
  session (8.11) one modal blocks every client, which is the ruling's
  own consequence, not the mirror's.
- **What a client may write.**  A panel write is a write to a real
  widget, and lands in the document through the panel's slots with
  the desktop user's power: the position of 8.11 (one shared session,
  the client IS the desktop user) already, and the command allowlist
  of 7.18's `runCommandOp` stays the gate for tool bar clicks.  New
  here: preference `Preferences/Fw/PanelMirror` (`all` | `none` | a
  list of dialog class names), default `all` under the shared
  session; a per-client grant is 8.12's multi-user work, not this.
- **Subscription.**  `widgets.subscribe` gains `panels` beside
  `toolbars` and `all`; `wants()` answers a `panels` subscriber for
  the mirror's ids; the mirror starts with the first subscriber and
  stops with the last (7.18's `checkMirror`), and a subscriber that
  arrives while a panel is up gets the snapshot in `snapshotOrder`.
- **The DOM side** is G7's (docs/ThinClient.md, the ThinClient
  session): the class set from `widget-models.json` (7.18 (c)),
  `VBox`/`HBox`/`Grid`/`Form` as flex and CSS grid, the panel
  container with the box headers and the dialog buttons, an image
  view for the picture model, item views over the row tree.
  7.12's number, 1.5 to 2k of TypeScript, stands; nothing in this
  section is spent on it.

**Stages and gates.**

    M1  the walk, bind, adopt, layout, the TaskBox root, the Control
        hook, the watch and the structure watch, the input semantics,
        the `panels` subscription.  Gate: gtest `PanelMirror` in
        `FormWidgets.cpp` (a uic'd form under a TaskBox mirrored:
        every named child a model of the expected class, the layout
        spec matches the .ui, a `setText` from code arrives as an
        update, a client `text` write fires the form's `textEdited`
        slot, a hidden page shows on the stack's `currentIndex`);
        GUI gate `SandboxPanelMirror.py` beside `SandboxNative`: Pad's
        panel opened, `panel:1` snapshotted with its
        `QuantitySpinBox`, `Length` written through `applyUpdate` and
        read back from `Pad.Length`, then Draft's `task_orthoarray`
        natively and CAM's `TaskPanel` (the `.ui`-driven op panel)
        -- three workbenches, no edit to any.
    M2  item views reflected (Sketcher's constraint list, BIM's
        QStandardItem panels, CAM's tool table); the picture
        fallback with `widgets.image`.  Gate: the constraint list's
        rows and checks arrive and a check box write reaches the
        sketch; a `QSvgWidget` arrives as one image and re-sends on
        change only.
    M3  nested modals; mouse replay into pictures.  Gate: a
        `QMessageBox` from a panel slot arrives as `dialog:1` and its
        button click returns the exec code.
    M4  the measurement (sec 8): the re-read cost per repaint burst
        on the two panels above, the bytes per second of a panel at
        rest and while typing, against 7.18's tool bar numbers.

**Cost** (lines, new; the binder, the store and the stream are reused):

    the walk: class chain, bind, adopt, layout kinds, TaskBox root,
      the Control hook, ids, the `panels` subscription             600-800
    the watch: the event filter, the re-read/diff flush, the
      structure re-walk, releases                                   350-450
    input semantics: the edit signals, key replay, focus            150-200
    item views, reflect mode                                        350-450
    the picture fallback, `widgets.image`, quotas                   200-300
    nested modals (M3)                                              200-300
    tests: gtest, the GUI gate, the CAM and Draft panels            500-700
    total                                                           2.4-3.2k, plus tests

Rough scale at sec 7's pace: M1 one to two sessions, M2 one, M3 and
M4 one.  Against the alternatives: the shim route (sec 11 item 5,
route A) costs a native op binding of the same order PLUS an edit per
residue file per workbench and gives no C++ panel; the H2 and H3
ports (7.12) are 10 to 15k touched for the desktop's Qt-free future
and now optional for the browser.

**Limits, stated.**  Desktop-with-Qt hosts only.  A custom-painted
leaf is a picture, not a control, until M3's replay.  A panel's
keyboard shortcuts and event filters see replayed keys only where the
mirror replays them (Return, Escape, Tab); a client's typing lands as
`text` writes, not keystrokes.  Widgets a panel creates and shows
OUTSIDE the task view (a floating tool window) are M3's modal walk or
nothing.  Every upstream change to a panel is picked up by the walk
for free -- the retire-on-break cost of the frozen guest path (1.7)
does not exist here, which is the second reason the route won.

**M1 built 2026-09-10** (`src/Gui/Fw/FwPanelMirror.*`, 1.0k lines;
`Gui::Fw::PanelMirror`, one per process, started by the first
`panels` subscriber and stopped by the last, `Preferences/Fw/
PanelMirror` the gate).  What the build settled beyond the sizing,
most of it asked by the ThinClient session's twelve requirements the
same day:

- **The tree and its ids.**  `panel` (the list, a VBox of the dialogs
  up), `panel:<n>` (a `QDialog` model whose `qtClass` is the
  TaskDialog's class -- `PartDesignGui::TaskDlgPadParameters`,
  `Gui::TaskView::TaskDialog` for a Python panel here -- and whose
  `windowTitle` is the first box's header), `pw:<n>` per widget; both
  counters per process, never reused.  Every open carries `parent`,
  which needed the store to learn a quiet adopt (`adopt(id, w,
  false)` + `announceOpen(id)`): the whole tree is registered first,
  then announced in post-order, the root last, so a parent ref never
  dangles and a subscriber mounts once.  The root's close is the only
  close: the children are released without a message
  (`Store::release(id, false)`); a widget that goes MID-LIFE (a page
  the panel deleted) is closed explicitly after its container's
  layout update stopped naming it.
- **The class chain.**  `tableClassOf` walks the real meta-object
  chain until `Fw::createWidget`'s table answers, and `qtClass` keeps
  the REAL name (`Gui::PrefQuantitySpinBox` on the `QuantitySpinBox`
  model) -- except a `TaskBox` subclass, which is
  `Gui::TaskView::TaskBox` to a client whatever the workbench derived
  (Pad's content is a `TaskBox`-derived panel, and a client keys the
  box chrome on that name).  A `QToolBox` lands on the `QTabWidget`
  model (`tabs` = its item texts, pages walked).  A container is
  walked; a leaf is not, and a plain `QWidget`/`QFrame` with neither
  a layout nor content children is a leaf (M2's picture).  Qt's own
  machinery (`qt_*`-named children, windows, menus, grips, focus
  frames) is skipped.  The pages of a tab widget, a stack, a tool box,
  a splitter, a scroll area's content and a button box's buttons are
  walked as a VBox/HBox of pages; a hand-built container's stray
  children join its layout after the placed ones.
- **The layout spec** grew what the DOM asked: per-item `stretch` and
  `align` on box items (`Layout::setItemStretch/setItemAlignment`),
  a grid's `columnStretch`/`rowStretch`/`columnMinimumWidth` and
  split spacings as layout-level extras (`Layout::setExtra`), a form
  row's `pos` as `[row, role]`, a spacer as `[w, h, hPolicy,
  vPolicy]` (already), margins and spacing always (what the real
  layout has).  `QFrame` gained `frameShape`/`frameShadow`/
  `lineWidth` (a .ui "Line" is a frame with shape 4 or 5).
- **The read.**  `FwQt::View::readProperties(widget, keys)` is the
  binder's meta-object read made public; the mirror adds `visible`
  (`!isHidden()`, so a stack's other pages are sent hidden), `font`
  as `{bold, italic}`, a combo's `items`, a tab widget's `tabs`, a
  TaskBox's `title`/`checkable`/`checked`/`flat`.  The watch marks
  dirty on Paint, Show, Hide, EnabledChange, ToolTipChange,
  FontChange, StyleChange, LanguageChange, WindowTitleChange,
  ReadOnlyChange, PaletteChange; the 0 ms flush writes the diff with
  the backend detached (the tool bar mirror's `writeDiff`).  A
  label's `setText` from code arrives as exactly one update, on the
  label's repaint.  `ChildAdded`/`ChildRemoved` of a widget child and
  `LayoutRequest` schedule a full re-walk, signatures per container
  deciding which layouts are re-sent (one `layoutSpec` update for a
  widget added by code, after its open).
- **A client's write** was NOT reaching real widgets before this:
  `Store::applyUpdate` wrote as `Source::Backend`, which a bound
  view by design does not apply (a backend reports what the widget
  already shows).  7.18's gates never caught it because the tool bar
  gate's writes went through `trigger` customs and the gtest's models
  were unbound.  `Source::Client` (3) is the fix: applied by the view
  like a native write, touching nothing, echoed to the guest sink.
  On such a write the mirror relays what the setters do not fire --
  `textEdited` on a line edit, `activated` on a combo -- once (the
  bound view hears the relayed signal as the user's edit and would
  write again; a guard stops the loop that took the first run down
  by stack overflow), and marks the widget dirty so the widget's own
  formatting (`10` written, `10.00` read) reaches the writer under
  origin 0.  Requests: `click`/`toggle` on a button, `editingFinished`
  and `returnPressed` replay Return, `escape` Escape; on the root
  `accept`/`reject` click the button box's Accept/Reject-role button
  (else `Control().accept()/reject()`), `clicked [flag]` that button,
  `helpRequested` the Help button -- all through the real button, so
  `TaskView::accept` and the dialog's own `accept()` run as for a
  desktop click.  A close the client's own request caused is
  announced under origin 0 (the first gate run lost the writer's
  close to the origin skip), and so are the opens of a dialog a
  client's click put up.
- **The subscription.**  `widgets.subscribe {"panels": true}`; the
  reply carries `panel` (the root id up, or null) so a client tells
  "none" from "pending"; `wants()` answers a `panels` subscriber for
  the mirror's ids.  The `Control` hook is `signalShowDialog` (the
  walk deferred one tick, after `modifyStandardButtons` and `open()`
  ran) and `signalRemoveDialog` (synchronous, before the widgets
  die); `start()` mirrors a dialog already up at once.
- **Gates**: `FormWidgets.cpp` `test_panelMirror` (20 cases now: the
  OrthoArray .ui through the host's `UiLoader` under a real
  `TaskBox`, a hand-built box, a real button box; every named child
  its model with the real class, the nested `grid_X` positions as
  the real grid has them, the stretch and alignment, the stack's
  hidden page, the opens in reference order with parents, a
  `setText` as one update, a client's text write firing `textEdited`
  and `editingFinished` on replay, a combo write firing `activated`,
  the stack's page shown under origin 0, reject and OK through the
  root, a widget added by code as one open plus one layout update,
  hide as one close).  `SandboxPanelMirror` (3, the GUI gate's
  default list, last, no guest): Pad's C++ panel (`lengthEdit` as a
  `QuantitySpinBox` model bound to `Length`, `q_rawValue` 25 written
  through the stream lands in `Pad.Length` through the panel's slot,
  the formatted text comes back to the writer, accept through the
  root, one close), Draft's OrthoArray natively (the form, the grid,
  a spin box written), a CAM Profile op (`Path.Op.Gui.Base.Create`
  with `res.job` set as the command's `Activated` would: one box
  holding the `IconTabWidget`, its pages walked, `startDepth` and
  `finalDepth` as quantity models) -- three workbenches, no edit to
  any.  Hooks on `FreeCADGui.FormWidgets`: `mirrorPanels`,
  `panelFlush`, `panelId`.
- **Not in M1** (as sized): item rows (M2: a mirrored list is its
  columns and headers today, the rows come with reflect mode), the
  picture fallback and `widgets.image` (a leaf with no model is a
  bare `Widget` with the real class and no pixmap), icons on buttons
  and labels (a `QIcon` has no name to send; M2's picture or a
  command's pixmap), nested modals (M3), the measurement (M4).  A
  TaskBox's header icon is likewise nameless and not sent.

**M2 built 2026-09-10** (`FwQt::View::reflectItems`, `Fw::ImageStore`
in `src/Gui/Fw/FwImage.*`, the mirror's picture leaf, `widgets.image`;
about 650 lines plus 450 of tests).  What the build settled:

- **Reflect mode** (`View::reflectItems`, the mirror calls it after
  `bind` on every `QAbstractItemView`).  Ids are minted per row of
  the REAL model (a `QPersistentModelIndex` each, the binder's own
  map), the tree is read once -- every column's `DisplayRole` text
  (a `setItemWidget` cell with no text takes its widget's `text`),
  the decoration as an `img:` id, tool tip, check state, foreground
  and background as color lists, bold, alignment, the row's flags
  (a cell's only where they differ from column 0's), `hidden` from
  the view, `expanded` from a tree view, children recursively -- and
  goes into the model as one `insert`, which the store's snapshot
  then carries as `items` in the open.  From there `rowsInserted`,
  `rowsAboutToBeRemoved` (the ids read before the indexes die),
  `dataChanged` (a `set` per cell, skipped when the model's cell is
  equal already, which is how a client's own set is not echoed while
  a panel slot's further change in the same call is) keep it current;
  `rowsMoved`, `modelReset` and `layoutChanged` re-read the whole
  tree as a `clear` and an `insert` (a `QListWidget::clear()` is a
  reset).  An op read from the real model is not applied back to it
  (`fromReal`); a client's op goes the existing way, `applyCustom`
  -> the model -> `View::applyItemOp` -> the real model's `setData`,
  whose `itemChanged` runs the panel's slot -- Sketcher's constraint
  check toggles the constraint's virtual space through
  `onListWidgetConstraintsItemChanged`, unedited.  A reflected op
  is announced under origin 0 whoever caused it: that slot runs
  inside the client's own write, and what it changes in the list
  must reach the writer too (the first gate run lost it to the
  origin skip, as M1's close had been).  What no model signal
  carries (`setRowHidden`, a tree's expansion from code) is re-read
  on the view's repaint (`syncReflectedRows`, `row` ops).
- **The store learned item ops.**  `Store::watch` had never announced
  `ItemView::itemsChanged`: a native model's rows reached its own
  view and no one else.  Now every item op fans out as `custom
  {item: ...}` (the same shape a client writes, so the wire is
  symmetric), to the guest sink too unless the guest's own comm is
  applying it; `snapshot(id)` carries `items` for a view with rows.
  And a QUIET adopt (`adopt(id, w, false)`) now holds every message
  about the object until its `announceOpen` -- a reflected view's
  first insert fired while the mirror was still registering the
  tree, before the open that carries the same rows.
- **The picture leaf.**  A widget whose class chain lands on
  `QWidget` with neither a layout nor content children is mirrored
  as a `QLabel` model with the real class as `qtClass` and `pixmap`
  = `img:<sha1 of the PNG>`; `QWidget::render` into a 1x pixmap of
  the widget's size (the longest side capped at 1024, scaled down
  past it), on the widget's paint, coalesced with the flush, at most
  once per 100 ms per widget (a grab that comes too soon is deferred
  to the interval's end, never dropped, so the last paint of a burst
  is what a client sees), 32 pictures per panel (the rest stay bare,
  logged once).  The grab's own render paints the widget: `markDirty`
  ignores it.  A hidden widget is not grabbed until it shows.  Same
  bytes, same id, nothing sent -- the gtest's leaf repainted
  unchanged sends no update, recolored sends exactly one.
- **`Fw::ImageStore`**: PNG bytes by content id, the newest 512
  kept, an icon registered once per `QIcon::cacheKey` and size, a
  pixmap once per `QPixmap::cacheKey` (a label's pixmap re-read on
  every repaint costs one hash lookup).  `widgets.image {name}`
  answers base64 PNG with `width` and `height`, `UnknownImage` for
  an evicted or unknown id (`name`, because `id` is the request's).
- **Icons by id**: a button's `icon` at its `iconSize`, a label's
  `pixmap`, an item cell's `icon` at the view's `iconSize` (16 px
  when unset) all travel as `img:` ids the same way; the DOM fetches
  `img:` where it would have asked `widgets.icon` for a name.  Not
  a box's header icon: the `QGroupBox` model has no key for it.
- **The viewport.**  A scroll area (an item view, a text edit) paints
  its viewport, not itself, so the M1 watch never saw a list repaint;
  the filter now sits on the viewport too and its paint is the
  view's evidence.  The mirror's `read` adds `columns` and
  `columnCount` for a view (no Q_PROPERTY carries the header).
- **Gates**: `FormWidgets.cpp` `test_panelMirrorItems` (a checkable
  list with an icon and a tool tip, a two-column tree with an
  expanded parent, a painted leaf, an icon button, a pixmap label
  under a TaskBox: the rows in the snapshot and the open with nothing
  sent before it, a client's check landing in the real item and
  firing `itemChanged` once under the client's origin, `addItem` /
  `setText` / `takeItem` / a tree child / `clear` as one op each, a
  collapse from the widget and an expand from a client, a hidden row
  found on the flush, the picture's PNG at the widget's size and
  color, no update on an unchanged repaint and one on a recolor, the
  icon and pixmap encoded once, the image op and UnknownImage).
  `SandboxPanelMirror` gained `test_sketcher_constraints` (the
  rectangle sketch's constraint rows with checks in the list's open,
  a client's uncheck putting constraint 0 in virtual space, the
  rows whole after the panel's own update) and `test_svg_picture` (a `QSvgWidget` in a Python
  panel: `QLabelModel` with `qtClass` `QSvgWidget`, the PNG 64x64
  through `widgets.image`, no update on `update()`, one on a new
  SVG).  A harness fact the second case exposed: the gate runs every
  test inside one slot of the main loop, so a closed dialog's
  `deleteLater` (posted at that level) never ran on its own and
  Sketcher's constraint list kept painting its gone sketch (a
  segfault in `ConstraintItem::data` when the next panel repainted
  the task view); the gate's `spin` now flushes deferred deletes
  explicitly, as the desktop's loop would.
- **The client's side of the rule** (asked by the ThinClient session
  the same day): a client's own write -- an item op or a `q_` key --
  is NEVER echoed back to it; the fan-out skips the writer and the
  real widget's answer is dropped when it equals what the model holds.
  A client applies its own write locally first and then whatever
  arrives under origin 0 on top, which is only what the panel changed
  beyond the write (a reformat, a slot's follow-up).  A write the
  real model refuses (`setData` false on a non-checkable item, a
  read-only cell) gets no reply either: a client gates its editors on
  the row and cell `flags`.  `UnknownImage` means keep the bytes you
  have: eviction is by count (the newest 512), so only a closed
  panel's ids fall out in practice, and the store re-files the bytes
  on that widget's next grab or icon read.
- **Not in M2**: mouse replay into a picture (M3), a box's header
  icon, coalescing per-row ops (M4 measures Sketcher's list first),
  nested modals (M3).

**M3 built 2026-09-11** (`PanelMirror` grew the dialog roots and the
replay, about 300 lines plus 350 of tests; no new file).  What the
build settled:

- **Dialog roots.**  A top-level `QDialog` shown while the mirror runs
  -- a panel slot's `QMessageBox`, a non-native `QFileDialog`, a
  workbench's own dialog, modal or not, whoever put it up -- is a root
  `dialog:<n>` (n per process, never reused) in the `panel` list
  beside `panel:<n>`, the list's layout re-sent with it appended in
  show order.  Found by an application-wide event filter `start()`
  installs (every event in the process passes the mirror's filter: a
  widget it does not know costs one hash lookup, and only a Show on a
  window that is a `QDialog` schedules anything); walked on the tick
  after the Show, when the tree is complete and laid out (a message
  box sets its layout up in `showEvent`), inside the `exec()` loop
  when there is one (a nested loop serves timers and the socket).
  The root IS the window's model -- a `QDialog` model bound by
  `View::bind` to the real window, `qtClass` the real class,
  `windowTitle`, `modal` and `visible` read -- and its content is the
  window's real layout walked as any container's.  Qt names a message
  box's content `qt_msgbox_label`, `qt_msgbox_informativelabel`,
  `qt_msgboxex_icon_label` and `qt_msgbox_buttonbox`, which the
  `qt_` rule of M1 skipped as machinery: `qt_msgbox*` is content
  now.  The root goes on the window's Hide -- `done()`, a close, its
  deletion -- as one close under origin 0, the subtree silent, the
  list re-laid; the panel root and the other dialogs stay.  A dialog
  already visible when `start()` runs is mirrored at once; the
  `Preferences/Fw/PanelMirror` class list judges a dialog's class as
  it judges a TaskDialog's; a window the store already carries as a
  bound view's widget (a guest form realized by FwQt) is not mirrored
  twice.  The picture cap of 32 is per process across the roots up.
  The native file dialog is not a QWidget tree and stays the client's
  own path picker (the `FileChooser` model), as sized.
- **Requests on a dialog root.**  `accept`, `reject`, `done` and
  `close` are the bound view's (it holds the real `QDialog`);
  `clicked [flag]` and `helpRequested` are the mirror's, through the
  window's `QDialogButtonBox` as the panel root has them.  On a
  `QMessageBox` the button's click is its `done(button)`, so the
  panel slot's `exec()` returns the standard button the client chose
  -- the gate's condition.  The code itself is the caller's, not on
  the wire: `QDialog::done` hides before it emits `finished`, so the
  root's close is the last message about it.  `widgets.subscribe`'s
  reply carries `dialogs` (the roots up, in show order) beside
  `panel`; `FormWidgets.dialogIds()` on the Python side.
- **Mouse replay into a picture.**  `custom {event: "mouse", args:
  [type, x, y, button, buttons, modifiers]}` with `type` one of
  `press`, `release`, `move`, `dblclick`, `enter`, `leave` (`button`
  the one causing it, Left by default and none for a move; `buttons`
  the state after it, by default the button on a press and none on a
  release; `modifiers` a `Qt::KeyboardModifiers` int); `wheel` with
  `[x, y, dx, dy, buttons, modifiers]` (`dx`, `dy` the angle delta).
  `x`, `y` are the picture's pixels, scaled back to the widget's when
  the grab was scaled past the side cap; replayed by
  `QApplication::sendEvent` of a `QMouseEvent`, `QWheelEvent` or
  `QEnterEvent` into the real widget, whose own handlers run; its
  repaint is the next grab (rate-capped).  On a widget that is not a
  picture the request is ignored with a log: a control has its own
  ops.  Keys beyond Return and Escape are not replayed (as sized).
- **Gates**: `FormWidgets.cpp` `test_panelMirrorDialogs` (22 cases
  now: a `QDialog` shown beside a panel with a picture leaf -- the
  root with its class, title, modal flag and parent, the label and
  the Yes/No buttons by flag, every open with a parent and the root
  last, the list re-laid; `clicked [Yes]` through the root accepting
  it, one close, the widgets gone, the panel untouched; a
  `QMessageBox` exec'd, inspected and answered No from a 50 ms timer
  inside its loop, the exec code; the preference class list refusing
  a dialog; press, move, release and wheel replayed into the picture
  at its pixels and not into a box; stop with a dialog up closing it
  and following nothing after).  `SandboxPanelMirror` gained
  `test_nested_messagebox` (a Python panel whose button slot execs a
  `QMessageBox` under the main window: a client's click on the
  button blocks in the slot, a timer inside the loop finds
  `dialog:<n>` with `qtClass` `QMessageBox`, `qt_msgbox_label`'s
  text, the buttons, the opens with the root last, and clicks Yes
  through the root; the slot gets 0x4000, the close reached the
  writer, the panel is still up); `subscribe`'s reply checked for
  `dialogs: []`.
- **Not in M3**: the measurement (M4); a box's header icon;
  coalescing per-row ops; a dialog shown from a thread other than the
  GUI's (Qt forbids it anyway); the ThinClient DOM's rendering of a
  `dialog:<n>` root (its side).

**M4 measured 2026-09-11** (sec 8.4 has the table).  The mirror grew
counters, nothing else: `PanelMirror::stats()` / `resetStats()` --
flushes, widgets re-read, keys read through the meta-object, keys
that differed and went out, the time in the reads, in the store
writes (the fan-out included), in the walks and in the picture grabs
-- read from Python as `FormWidgets.panelStats(reset=False)`; the
wire is counted by the pushed log's JSON, byte-exact.  The rig is
`src/Mod/Test/SandboxMirrorBench.py`, a GUI gate module NOT in the
default list (a measurement, like the `DISABLED_` bench gtests; name
it alone in `SANDBOX_GUI_GATE_MODULES`), two subscribers so that the
wire a write costs is what the OTHER client gets (the store skips
the writer).  What the numbers decide:

- **The watch is cheap.**  A repaint burst with nothing changed
  (every content widget's `update()`, the children painting with the
  parent's region) re-reads 10 to 20 widgets, 150 to 350 keys, in
  0.3 ms, writes no key and sends no byte -- on Pad, on OrthoArray,
  on Sketcher's list alike; five spaced bursts cost five times that
  and nothing on the wire.  At rest a panel sends nothing for two
  seconds and re-reads nothing (Pad's first run showed four flushes
  of three widgets in two seconds, the cursor blink, 0 bytes; the
  second run none -- the field had no focus).  The sizing's
  "microseconds per widget" holds: 15 to 25 us a widget.
- **A keystroke is one update to each other client.**  A client's
  write lands in the real widget, the writer is skipped, the others
  get the changed keys: 71 B a keystroke on OrthoArray's count spin
  box (`q_value`), 76 B on Pad's length field, plus 76 B back to the
  writer when the field REFORMATS its text (`q_text` "10.5 mm"
  differs from what the writer sent) -- ten keystrokes in 0.54 s are
  1.5 KB to a watcher, 0.8 KB to the writer, 1.6 ms of re-reads and
  0.3 ms of writes for Pad's recompute on each.
- **The open is the cost.**  Pad's panel is 61 models, 64 messages,
  46 KB, the walk 3.2 ms; OrthoArray 46 models, 48 messages, 35 KB,
  2.5 ms; Sketcher's panel 29 models, 31 messages, 30 KB (48 rows in
  the list's open), 4.1 ms -- 700 to 1000 B a model, the layout spec
  and the whole bag.  The close is five messages under 500 B.
- **Sketcher's refill is the one hot spot.**  A check toggled from a
  client costs the other client ONE `item:set` of 107 B (the panel's
  in-place update finds nothing else changed).  But a solve -- a
  point moved, the sketch recomputed -- refills the list in place
  and sends 162 item ops, 21 KB, to every client: two `clear`s, 32
  `insert`s, 128 `set`s for 48 rows, 0.13 s end to end.  Dragging a
  point at 60 Hz would be 1.3 MB/s per client.  That is the number
  for coalescing per-row ops (the gap sec 13 lists): a refill should
  go out as one `items` reset when the ops outnumber the rows, which
  here they do 3:1.
- **Against 7.18's tool bars.**  The tool bar subscription's open is
  the biggest burst in the system: 202 messages, 178 KB (Draft's
  bars, every command's bag with its icon name, tool tip and status
  tip); at rest nothing; the selection timer (`testActive` every 150
  ms) with a selection changing ten times in two seconds sends 110
  updates, 10 KB, 4.6 KB/s -- the enabled flags of the commands that
  care; a switch to Part is 67 messages, 51 KB (56 opens, 10 closes,
  one order), back to Draft 15 messages, 10 KB (Part's bars stay
  made; the close is cheap).  A panel costs a fifth of a tool bar
  subscription to open and, typing, a third of what the selection
  timer costs idle.
- **Not measured**: a picture leaf's grab under a moving scene
  (`grabUs` is counted, the bench has no picture); a non-native file
  dialog's rows; the DOM client's own cost of applying an open.

### 7.20 The browser console sized: pyodide in the page, the wire over the socket **[sized 2026-09-11; RULED: after the document program; C1 BUILT 2026-09-14, JSPI proven; C2 BUILT 2026-09-15, the bridge over the socket; C3 BUILT 2026-09-15, the client principal; C4 BUILT 2026-09-15, the console panel; C5 BUILT 2026-09-15, the latency and the page's memory measured, and a prefetch of sibling reads]**

The question, asked before the document program (7.17) was started:
how far is a Python console in the browser tier -- pyodide running in
the CLIENT's page, the desktop console's reach, under the sandbox's
access control -- and what would it take.  Sized here; not built.
**Ruled 2026-09-11: client-side pyodide is the target, and it comes
after the document program.**

**The short answer.**  The back end exists and runs on the desktop
today; what is missing is moving the guest from the host's V8 into
the page: the wire carried over the WebSocket, a principal for a
remote client, and the panel.  One to two weeks to a first working
version with access control, with one unproven piece (the suspending
import inside pyodide's dynamic linker).

**What is already built and reused as is.**

- *The guest is a console back end.*  The session document (S1:
  live `ActiveDocument`, `listDocuments`, `newDocument`, `save`,
  picker-blessed `saveAs`), `Gui.doCommand` / `addModule` executing
  inside the guest (S2), `Selection` with observers, `Control`,
  `runCommand`, the `Command` list, the stock dialogs, 345 annotated
  members across the generated facades (3.3) plus the `Part` and
  `TechDraw` module facades.  The `exec` op runs statements as the
  session principal (`FreeCAD.ExpressionSandbox.exec`).  That is the
  desktop console's surface minus what the catalog denies by design.
- *The wire is transport-neutral.*  Every op is CBOR bytes in, CBOR
  bytes out (`FcxWire.h`); the host's dispatcher is a plain `BridgeFn`
  of bytes to bytes (`ExpressionImageRuntime.h`), and host->guest is
  `roundTrip(bytes)`.  Nothing in the protocol assumes in-process
  transport except the per-transaction handle table.  The page already
  has a CBOR codec and an image loader
  (`src/Gui/Renderer/web/src/sandbox/`).
- *Enforcement is host-side only and the guest is assumed hostile*
  (2.4): principals, catalog, grants, audit, the per-op schema and
  permission checks all sit on the host, and `check*` compiles to a
  no-op inside the image.  A browser guest is untrusted by
  construction, which is the assumption the model already makes.
  Nothing in the security stack moves.
- *The door exists.*  `SceneStreamServer` admits a connection through
  the grant list (docs/ShareAccess.md sec 2), carries a verified
  identity from the front door, a view-only flag and a stable
  connection id on every control request (`SceneClientInfo`,
  `dispatchControl`), and its HTTP endpoints sit behind the same gate.
  docs/SandboxNetwork.md 9.6 already designs `/pyodide/...` and
  `/packages/...` served by the serving FreeCAD.
- *Pyodide 314.0.6 is pinned* (5.3), and that release ships
  `pyodide.console.Console` (a REPL: incomplete-input detection,
  completion, stream redirection, top-level await) and JSPI support
  (`run_sync`, `enableRunUntilComplete` on by default).

**The gaps, largest first.**

1. *The bridge across the WebSocket.*  The guest's host call is a
   synchronous wasm import (`fcx_host_call` / `fcx_host_fetch`, 4.1),
   and in the page the host is at the far end of an asynchronous
   socket -- why 4.3 leaves the bridge unattached.  Two ways to close
   it:
   - **JSPI** (JavaScript Promise Integration): wrap the import as a
     `WebAssembly.Suspending` function that awaits the round trip.
     Shipped in Chrome 137 and Firefox 139, on by default on every
     Firefox platform from 153; the browser tier already requires
     exnref (Firefox 131+, Chrome 137+), so this is a compatible
     floor.  The unproven piece: the import is bound by pyodide's
     dynamic linker when the side-module wheel loads, and the V8 host
     already hooks that point (`Module.mergeLibSymbols` before the
     wheel loads, 4.1) -- the same hook should take a suspending
     function, but it has not been tried.  The two-call pattern (call
     then fetch) collapses to one await.
   - **A Worker with `SharedArrayBuffer` and `Atomics.wait`**: every
     browser including Safari, which has no JSPI.  Costs cross-origin
     isolation headers (COOP/COEP) on the served page, not set today,
     and the guest in a Worker with the panel on the main thread.
     The mobile tier includes iOS, so this path has to exist
     eventually; JSPI is the faster first step.
2. *A host-side session per remote guest.*  `ImageHost` is a
   singleton: one guest, one handle table cleared per transaction, a
   scope stack that holds for the whole synchronous call
   (`Transaction`, `clearHandles`).  A remote statement spans many
   round trips and the GUI thread cannot block on a socket.  The new
   piece is a per-connection endpoint on the host: its own
   `HandleTable` and principal, bridge ops arriving as binary control
   frames, the `Runtime::Scope` pushed PER OP, dispatched into the
   same `BridgeFn` the local runtime uses; the guest's release queue
   ("r" on the next request) keeps the table bounded and a statement's
   end clears it.  The durable document-object keys (3.2, `resolve`)
   already tolerate the desktop mutating between two ops of one
   statement.  The host's own document guest stays where it is and
   keeps serving Proxy hooks, so the two guests coexist.  One new
   edge: a remote guest reading `obj.Proxy` receives a `gproxy` tag
   from the OTHER guest's registry, which it cannot resolve --
   answer it as `unsafe.getattr` (DENY) or by value.
3. *A principal for a client, and the grant UI.*  A remote user is a
   fourth class, `client:<identity>` (the front door's verified
   identity; the admitting grant's id when there is none), with its
   own catalog column.  The mapping: `doc.read.self`, `app.query`
   ALLOW; `doc.write.self` follows the connection's view-only flag;
   `doc.foreign` follows the multi-document serve grant; `app.write`,
   `gui`, `gui.doCommand`, `prefs.write` PROMPT on the desktop or
   DENY; `unsafe.getattr` DENY; `fs.*` / `host.exec` DENY, not
   promptable.  The catalog is frozen v1 and `grants.json` is schema
   v1: this is v2.  The 7.14 chokepoints are designed, not built, and
   must be built -- or `gui` must be a hard DENY for clients -- before
   a remote user reaches `Gui.runCommand` (F1 was DROPPED in sec 11
   as a session-only need; a remote client is the second guest with
   that need).  The prompt appears on the DESKTOP (the owner
   consents); `PermissionNeeded` fails fast as it does today, the
   client sees the refusal, the owner grants, the client retries.
4. *The console panel.*  `pyodide.console.Console` over a DOM panel
   (the panel infrastructure of `web/src/panel.ts`), stdout and stderr
   redirected into it, `Console.complete` for completion, history in
   `localStorage`, the runtime, the `fcx_image` wheel and the bundled
   widget wheels booted from the serving FreeCAD (9.6 source 1) or
   jsDelivr for a static page.
5. *Latency and memory, unmeasured.*  A desktop hop is ~7 us (8.1);
   over a LAN it is a millisecond, through a Cloudflare tunnel 30 to
   100 ms.  A statement touching a few dozen properties is fine; a
   loop over a thousand edges is seconds.  The snapshot op that 8.1
   keeps optional becomes necessary here, and the fixed layout for
   the hot ops (`FcxWire.h`) buys nothing against RTT.  The guest is
   335 MB at boot on the desktop (7.17); the page's figure, and a
   phone's, are to be measured before the panel is offered there.

**What a remote console would NOT have.**  Everything not annotated
(absent means DENY), `unsafe.getattr`, and any view API:
`ActiveView` is the mirror's on the desktop (G4), but in the browser
the view is the CLIENT's own, so `fitAll` / `viewAxonometric` / the
camera are a small LOCAL facade talking to the viewer in the page,
not an op to the host.  The macro echo the desktop console shows
(`ScriptToPyConsole`, `Macro.cpp`) would be a subscription to the
macro manager's stream -- new, small.  A guest-built form from a
client needs the widget layer's DOM backend (G7), not built: out of a
first version.  Undo: the desktop console's `Gui.doCommand` opens no
transaction of its own; whether a remote statement should wrap its
writes in one (`openTransaction` is not in the facade today) is a
decision for the build.  Under the shared session (docs/ThinClient.md
8.11) a client's writes land in the one document every view shows.

**Order and cost.**

    step                                                          size
    ------------------------------------------------------------  ----------
    C1  serve pyodide and the wheels from SceneStreamServer,      ~1 day
        boot the guest in the page, bridge unattached (the
        9.6 flow; pyodide's own browser loader)
    C2  the remote bridge: binary control frames, the             2-3 days
        per-connection endpoint on the host, the JSPI wrap of
        the import, a gate (a statement reading and writing a
        served document from a page, the desktop seeing it)
    C3  the client principal, catalog v2, view-only mapping,      1-2 days
        the 7.14 chokepoints (or gui DENY for clients)
    C4  the console panel                                         ~1 day
    C5  measure hop latency (LAN, tunnel) and the page's memory;  1-2 days
        the snapshot op if a typical statement is too slow
    C6  the Worker + Atomics path for Safari; COOP/COEP on the    1-2 days
        served page                                               (later)

**The alternative, named because it changes the cost picture.**  Keep
the guest on the HOST as a second V8 pyodide instance per client and
stream only text: the security stack applies unchanged, latency
disappears, the page needs a text panel and nothing else -- two to
three days.  The price is 335 MB of host memory per connected client
(the cost 7.17 ruled per-document guests out on) and no isolation of
a runaway client's CPU from the desktop.  Client-side pyodide is the
end state for those two reasons; the host-side variant is the cheap
fallback if the JSPI wrap turns out to be a fight.

**Gate `SandboxBrowserConsole`** (C2 and C3 together): a page's guest
evaluates `FreeCAD.ActiveDocument.Objects` over the socket; a
view-only client's `write_prop` is refused with the client principal
in the audit line; an editing client's `Part.makeBox(10,10,10)` bound
to a new object appears in the desktop's tree; `Gui.runCommand(
"Std_RecentMacros")` from a client is refused, not promptable; a
second client's statement interleaved with the first's is answered
under its own principal; the desktop's own console is unchanged.

Sources: Firefox's JSPI release bug (bugzilla 2044809), the V8 JSPI
introduction (v8.dev/blog/jspi), Chromium's intent to ship, pyodide's
JSPI post (blog.pyodide.org/posts/jspi) and changelog.

**C1, BUILT 2026-09-14.**  The guest boots in a page from the FreeCAD
that serves the document, bridge unattached.  Two probes came first,
both in headless Chrome 153 against a plain static server: pyodide
314.0.6 and the fcx_image wheel boot in the page unchanged (1273 ms for
the runtime, 118 ms for the wheel and the `_fcx_image` import) and
`fcx_call` round trips answer (`1 + 2 * 3`, a quantity); and **the one
unproven piece of this section is proven**.  `fcx_host_call` merged as a
`WebAssembly.Suspending` through `mergeLibSymbols` -- which stores what
it is given as is, and pyodide imports a Suspending of its own already
-- with `fcx_call` entered through `WebAssembly.promising`: a CBOR op and
a fixed-layout op, both called from a Python statement deep inside
CPython, suspended on a 30 ms promise and resumed with the reply.  So C2
is not a JSPI fight, and the host-side alternative above is not needed
for Chrome.

*The server* (`SceneStreamServer::setHttpMount`, a generic mount: a
prefix, a provider, gated or not; the path gets the viewer bundle's
checks first).  `Gui::SandboxServe` (src/Gui/SandboxServe.cpp) mounts,
from every `SceneServeSource::serve` -- `Gui.serveDocument` and the
share panel alike:

    GET /pyodide/boot.json                  behind the door, no-store
    GET /pyodide/runtime/<version>/<file>   the pinned runtime files, max-age
    GET /pyodide/wheels/<file>              fcx_image (no-store), bundled wheels
    GET /pyodide/packages/<file>            the package set, closed over the lock

boot.json names what the DESKTOP runtime would boot with, resolved the
way it does (`ImageHost::location`, `Pyodide::layout`, the pinned table
and its `PyodideUnpinned` override, the ABI match), relative to
/pyodide/; a box with nothing to serve answers 503 with the reason.  It
is resolved on the GUI thread per serve and answered from that snapshot:
no request ever takes the evaluation lock.  The files are served AHEAD
of the door, like the viewer bundle, and nothing but the files boot.json
names: they are published code, and they cannot be gated anyway --
pyodide fetches the runtime by URL arithmetic that drops a `?token=`,
and `loadPackage` takes a URL for a wheel only when it ENDS in `.whl`
(`uriToPackageData`).  boot.json is gated because it names the owner's
package set; the package files themselves are pyodide's own public
wheels.  The version sits in the runtime URL so those 13 MB can be
cached; fcx_image keeps its name across rebuilds and is never cached.
A path the ungated mount declines goes to the door: 403 without the
token, 404 past it.  Compiled only with the pyodide host
(`FC_EXPR_PYODIDE_HOST`, scoped to the one source file).

*The page* (`web/src/sandbox/guest.ts`, `BrowserGuest`): the glue's boot
step for step -- runtime, the two imports merged, the wheel, the bundled
wheels and the package set by lock name, `_fcx_image`, the side module's
exports -- and `call()` in the desktop host's `roundTrip` sequence,
queued (one guest stack, and a suspended call still holds it).  A
`bridge` option already takes an async round trip and wires the JSPI
pair; C2 supplies it.  `console-test.html` is the gate page.

*Measured* (the browser leg, headless Chrome 153, swiftshader, served
by FreeCAD on loopback): runtime 1433 ms, fcx_image plus the six bundled
wheels (pivy included) 409 ms, guest linear memory 50 MB after boot.
The 50 MB is the wasm heap alone and is not comparable to the desktop's
335 MB, which is the whole V8 process; the page's process figure and a
phone's are still C5's.

*Gates*: `GuiSandboxConsoleServe` (registered, 33 PASS:
the door on boot.json, every runtime file and the wheel byte for byte
with the types a module loader insists on, the refusals);
`tests/gui/sandbox-console-browser.py` (not registered, docs/Testing.md:
node, puppeteer-core, a Chrome; 10 PASS -- served headless, the page
boots, JSPI present, an expression and a quantity, CPython 3.14, the
in-image FreeCAD module, the bridge unattached, every bundled wheel
loaded).  The browser tooling was gone from the box and was set up again
(Testing.md): node from emsdk, Chrome for Testing, and a `libasound`
symlink it needs.

*Not in C1*: the viewer page has no console (C4); the static-page source
(jsDelivr, docs/SandboxNetwork.md 9.6 source 2) and the Cache API
persistence are not
built; a remote client's `ActiveView` facade is C4's.

**C2, BUILT 2026-09-15.**  A guest in the page reads and writes the
served document over the socket, and the desktop sees it.

*The wire* (`SceneBridgeRequest`, src/Gui/Renderer/SceneServer.h):

    up    'S', kind u8, seq u32 LE, the request bytes as the guest wrote them
            kind 0  one bridge op, answered with the same seq
            kind 1  the end of a statement: no payload, no answer
    down  'FCSB', seq u32 LE, the reply bytes

The bridge protocol itself is unchanged -- CBOR or the fixed layout,
carried rather than translated.  An EMPTY reply means nothing serves the
bridge (no handler on the connection's group, or no sandbox host in the
build) and the guest raises "host bridge unavailable" at once instead of
waiting.  The server forwards in the connection's order and never
coalesces an answer: a new outbox kind, since a streamed frame replaces
the one still queued and an answer must not.  A connection with 64 ops
unanswered is kicked -- a guest waits on every answer, so one that far
ahead is not a guest.

*The endpoint* (src/Gui/SandboxRemote.cpp): one per connection, a
`HandleTable` of its own.  SceneServeSource installs the handler on the
document's group next to the control channel; every frame hops to the
GUI thread in arrival order and is dispatched by `dispatchHostBytes`,
the bytes-level entry ImageHost's own bridge now calls too, so the
desktop's guest and a page's run one dispatcher.  Each op runs under
`Runtime::Scope(const App::Document*)`, a new form: the served
document's principal with no owner object, pushed per op.  The document
itself is the table's owner, and nothing else needed to learn about
remote guests -- reach, the write gate and `resolve` already key on the
owner's document (`documentOf` takes a Document), and `active_doc`
answers an owner that is a document with itself.  An End clears the
table; what the guest keeps across statements re-resolves by its
durable key (3.2), which the page exercises.  A connection that
switches documents starts from an empty table, and one that closes
drops its endpoint (the group's closed handler).

*The principal, until C3.*  The document's own, not a client's.  A
connection admitted with edit access can already change the document
through the control channel, so this is no wider than what the door
gave it, and the catalog holds the rest: `app.write` and `gui` DENY and
not promptable, `app.query` and `doc.foreign` PROMPT, which the client
sees as a PermissionError.  What it conflates, both C3's: grants the
owner gave the document's own code apply to the client too, and the
audit line names the document, not the client.  A view-only connection
gets NO bridge -- every op a PermissionError -- until C3 maps its flag
onto `doc.write.self`.

*The page* (`web/src/sandbox/remote.ts`, `RemoteBridge`):
`connect(server, {token, doc})` opens a /scene socket of its own and
ignores the scene payload it is pushed; `attach(socket)` takes one
already open, the viewer's.  It implements `HostBridge` --
`roundTrip(bytes)` to a promise of bytes, and `endStatement()` -- which
is what guest.ts's `bridge` option now takes: transport-shaped, so C6
changes only the waiting side.  `BrowserGuest` ends a statement after
every call, and gains `exec()` for statements.

*Measured* (headless Chrome 153 on loopback, the browser leg): 80
bridge ops over 9 statements, 0.61 ms mean round trip, 3.8 ms worst;
2976 bytes up, 6276 down.  Twenty host calls inside one generator
expression each suspended and resumed.  The LAN and tunnel figures are
C5's.

*Gates*: `GuiSandboxBridgeServe` (registered, 17 PASS: the wire
from a plain socket client on a worker thread, no guest --
`active_doc` as the owner, a fixed-layout answer in the fixed layout,
call / write_prop / read_prop, three ops back to back answered in
order, the End releasing the handles and `resolve` after it,
`app.new_doc` and another open document refused, an undecodable request
a ProtocolError, a view-only connection refused in both layouts, a
connection joined to no served document answered empty, the desktop
carrying the write); `tests/gui/sandbox-bridge-browser.py` (not
registered, docs/Testing.md; 15 PASS -- the page's guest reads
`ActiveDocument` and its objects, writes `Box.Length`, adds an object,
keeps one across statements, runs twenty host calls in one expression,
is refused `newDocument`; the desktop sees the write and the object).
C1's browser leg, re-run over the changed guest.ts, still passes.

*Not in C2*: the client principal, catalog v2, a read-only bridge for a
view-only client and an audit line naming the client (C3); the viewer
page's own socket carrying the bridge -- that socket belongs to the wasm
viewer, and `attach` is the hook -- and the panel (C4); the cost of the
scene payload a console-only socket is pushed, unmeasured.

**C3, BUILT 2026-09-15.**  A page's guest acts as the remote USER, not
as the document it is served.

*The principal* (`clientPrincipalId`, ExpressionSecurity.h), made from
what the door knows of the connection at the frame -- SceneServer copies
it onto every `SceneBridgeRequest`:

    client:id:<identity>    a trusted front door's verified identity
    client:grant:<n>        none: the grant that admitted the connection
    client:conn:<n>         neither: the legacy single-token door

Three prefixes, not the `client:<identity>` of the sizing: an identity
is a front door's text, and one reading `grant:3` must not become the
grant's principal.  An identity carrying a control character falls to
the next form rather than being rewritten, which could make two people
one principal.  Only the first form names a person across runs: a grant
id is assigned per run and a connection id per connection, so a grant
for either is refused scope "always" (`isPersistablePrincipal`) -- else
tomorrow's grant 3 inherits today's answers.

*Catalog v2, the client column* (`catalogDefault`, `isPromptable`, and
a new `isGrantable`):

    permission        client    promptable  grantable
    ----------------  --------  ----------  ---------
    doc.read.self     ALLOW
    doc.write.self    ALLOW                 yes       DENY (np) on a view-only connection
    doc.foreign       DENY      no          yes
    geom.call         ALLOW
    app.query         ALLOW
    prefs.read        ALLOW
    prefs.write       DENY      no          yes
    app.write         DENY      no          yes
    gui               DENY      no          NO
    gui.doCommand     DENY      no          yes
    host.import       PROMPT    yes         yes       the OWNER is asked, on the desktop
    unsafe.getattr    DENY      no          NO
    pkg.install       PROMPT    yes         yes

The sizing left three choices open, decided here.  `gui` is a hard DENY,
not a prompt: the 7.14 chokepoints are not built, so `Gui.runCommand`
reaches `Std_RecentMacros` and a macro file runs as host Python.  "Not
grantable" is new and means no grant of any kind lifts it -- not a panel
answer, not grants.json, not a process `--grant` (`Runtime::resolve`
answers DENY before any of them, and `grant()` raises); it holds for
`gui` and `unsafe.getattr`, the two cells that run host code, until 7.14
lands.  `doc.foreign` is DENY because the multi-document serve grant IS
the switch (docs/MultiDocServe.md sec 4): a connection that wants another
document joins it, the door judges that, and the switch starts a fresh
table.  `app.write` and `prefs.write` are DENY rather than prompts: a
remote user has no business creating or closing the owner's documents
or rewriting the owner's preferences, and an owner who decides otherwise
grants it explicitly.

*View-only* is the scope's flag (`RemoteClient::readOnly`), not a
catalog cell: a view-only connection's `doc.write.self` is DENY, not
promptable, before any grant is consulted -- the door's access is the
owner's decision on the sharing roster, and a permission grant must not
be a second way around it.  The flag is read per frame, so a roster flip
takes effect at the client's next op.  The bridge is otherwise whole: a
view-only guest reads the document.

*Reach.*  A client is confined to its owner's document exactly as a
document principal is (`principalIsConfined`, the renamed
`principalIsDocument`) -- without it, a client would have fallen through
to the session's reach of every open document.  Three client-only edges:
a `gproxy` tag is refused both ways -- its id names an instance in the
DESKTOP guest's registry, which the sizing's item 2 flagged; reading one
is `unsafe.getattr` and a client passing one (or a `gmethod`) is a
PermissionError, since binding it would hand another object's Proxy to
this one -- and `saveAs` is refused outright, because the blessed-path
set is global and belongs to the desktop guest's pickers.  An `fcall` on
a routed function is left as it is: the call runs as its feature's file
(docs/ProxyChain.md 2.5), which is what that document's own code does on
a recompute.

*The store.*  grants.json stays schema version 1 on disk.  A v1 reader
already skips a grant whose principal form it does not know, which is
exactly the forward compatibility a client grant needs; writing version
2 would make every older build refuse the WHOLE store.

*The audit line* names the client twice over: the principal, and the
context `<Doc>: client #<conn> '<label>' @<address>`, with ` view-only`
appended on such a connection.  The label and the address are what the
client or a proxy declared, so the log's JSON dump now replaces bytes
that are not UTF-8 instead of throwing on the way to a refusal.

*The panel.*  A client's pending request (only `host.import` prompts)
lists under the served document with the raw principal.  Granting it
"always" on a run-local id, or granting a client `gui`, now raises; the
panel's grant action catches that and says why instead of letting it out
of a Qt slot.

*Gates*: `ExpressionSecurity.clientPrincipals` and the client rows of
`catalogDefaults` and `principalClasses`;
`ExpressionSecurityRuntimeTest.clientScope` (the client form pushes over
an active scope, "this client" in the message, a view-only write refused
through a session grant, the ungrantable cells refused and resolved DENY,
"always" refused for `client:conn:`, a null document still pushing);
`GuiSandboxBridgeServe` rewritten to 33 PASS (from 17): refusals now
the client's and not promptable (`app.new_doc`, `gui.cmd.run
Std_RecentMacros`, `saveAs`), `Part.makeBox` bound to a new
`Part::Feature` that the desktop's tree carries (volume 1000), a
view-only connection that reads and is refused its write, two clients
under different grants interleaved on the wire and refused under their
own principals, a client vouched for by `X-Forwarded-Email` with
`FC_SERVE_TRUST_PROXY=1` audited as `client:id:carol@example.com`, the
audit lines' principals and contexts, and a host call after the run
under no scope.  `wsclient.WS` takes extra upgrade headers for that.
The page leg, `tests/gui/sandbox-bridge-browser.py`, re-run over the
client principal: 15 PASS unchanged (80 ops, 3.3 ms worst).  Suites:
ExpressionSecurity* 22 OK, ExpressionImage*/ExpressionRouting* 98 OK,
ctest 641/641, SandboxProgram 46 OK, FeaturePythonChain 44 OK.

*Not in C3*: the panel (C4); the sizing's gate `SandboxBrowserConsole`
is covered item by item on the plain-socket gate, and the page leg
re-run over the new principal, but there is no page-side console yet;
the permissions panel shows a client's request but has no roster-aware
UI for identities; the 7.14 chokepoints, which would let `gui` become a
grantable row for clients; the latency and memory of C5.

**C4, BUILT 2026-09-15.**  The viewer chrome has a Python console, and
it runs in the page.

*The interpreter* (`web/src/sandbox/console.py`, pushed into the guest as
the module `fcx_console` by the exec op's `module`, once per boot): stdlib
`codeop` for incomplete input, `rlcompleter` for completion, `traceback`
for the report, a namespace of its own with `FreeCAD`/`App` and
`FreeCADGui`/`Gui`.  Each line is one eval op, `fcx_console.push(line)`
with the line as a binding, answering `ok`, `more`, `syntax` or `error`;
the traceback drops the console's own frame, so an error reads as it does
on the desktop.  The sizing named `pyodide.console.Console`, and it is NOT
used: it runs a statement as an asyncio task on pyodide's web loop, which
enters wasm through pyodide's own promising export rather than `fcx_call`,
so the statement would also bypass `BrowserGuest`'s queue and never end as
a bridge statement -- the host's handle table would only grow.  Through
the eval op every line is one guest call and one statement, the path C2
and C3 already gate.  No top-level `await` (nothing in the image wants
it).

*The session* (`web/src/sandbox/session.ts`, no DOM): the bridge socket,
the boot, the module, and the guest's `sys.stdout`/`sys.stderr` taken
over with pyodide's raw `write` handlers -- delivered as written, not per
line, so a loop printing at each host call shows its progress while it
runs.

*The socket is the viewer's.*  The first build opened a second connection
(`RemoteBridge.connect`), on the belief that the wasm viewer could not be
built on this box to take a hook.  It could -- nobody had configured
`build/wasm` here (emsdk-5.0.3 and the relwithdebinfo tree's shaderc, docs/
Testing.md) -- and a second connection was worse than a cost: it is a
second entry on the owner's roster, so the owner's view-only switch for
the viewer did NOT hold for its console; a token-only door made it a
different principal (`client:conn:<n>`), so a grant to one was not the
other's; and it was pushed the scene it ignored (3.6 KB for the gate's two
small documents, growing with the document).  Now wasm/main.cpp hands an
`FCSB` frame to the page as an `fc:bridge` event before the scene parser
sees it, sends a page's `'S'` frame through `window.fcviewerBridgeSend`
(nothing but an `'S'` frame), and reports the socket as `fc:socket` events
(mirrored on `window.fcviewerSocketOpen`).  RemoteBridge sits on a port:
`viewer()` for those hooks, `attach()`/`connect()` for a socket of its own.
A loss fails the pending ops -- a reconnect is a new connection whose
endpoint never saw them -- and on the viewer's port the bridge works again
once the viewer is back.  The viewer chrome asks for the viewer's socket
and waits up to 20 s for the hook; a viewer that never installs one gets a
connection of its own, and the console says so in red.  A document switch
is the viewer's own, the host drops the connection's endpoint, and the
panel says that names bound to the old document no longer resolve; the gate
page, which has no viewer, sends `{"cmd":"switch"}` on its own socket.

*Interrupt.*  pyodide checks an interrupt buffer at bytecode boundaries,
and the page can only write it while the guest is not running on the page's
thread -- which is exactly while a statement is suspended on a host call.
So the Interrupt button (and Ctrl+C) stops a loop that reaches the host,
raising `KeyboardInterrupt` at its next boundary, and the console is usable
at once; a pure CPU loop freezes the page, which only the guest in a
Worker (C6) can fix.

*Completion* found one gap: a proxy's `dir()` lists the members its facade
declares, and a property is not one -- it is read through `__getattr__`,
answered by the host -- so `b.Leng` completed to nothing.  The console's
completer adds the object's `PropertiesList` (annotated, value tier) to
rlcompleter's matches.  Nothing else is learned about proxies; a `dir()`
in user code still shows only the facade.

*The panel* (`web/src/console.tsx`): a draggable card at the bottom left
(the sheet panel holds the bottom right), a bottom sheet on a phone.  Booted
the first time it opens, not at load -- 1.7 s and the runtime's download
are not a viewer's cost unless the console is used -- and kept when closed.
Enter runs, Up/Down walk a per-browser history (localStorage, 500 lines),
Tab completes (the common prefix goes in, several candidates are listed),
Ctrl+C interrupts or drops the line, Ctrl+L clears, a paste of several
lines runs line by line as if typed.  The header shows the document and the
view-only badge; a view-only connection's console reads and is refused its
writes (C3).  A browser without JSPI gets the reason instead of a boot.
The launcher menu gains "Python console"; `?console` opens it on load.

*Measured* (headless Chrome 153 on loopback, the gate's drive): boot 1709
ms; 1349 bridge ops over the whole drive, 0.42 ms mean round trip, 5.6 ms
worst -- most of them the interrupted loop's.  In the real viewer page, on
the viewer's socket: boot 3.9 s from page load (the viewer's own start
included), 24 ops at 2.3 ms mean, 19 ms worst while the viewer streams its
first frames, and nothing but bridge answers handed to the bridge.

*Gates*: `tests/gui/sandbox-console-panel-browser.py` (not registered,
docs/Testing.md; 24 PASS) serves two documents and drives
`web/console-panel-test.html` -- the panel with no WASM viewer -- through
DOM events: the boot, an expression's repr, a name kept across lines that
writes `Box.Length`, a block on `...` run by its blank line, output with no
newline, a traceback without the console's frames, a syntax error followed
by a working line, `newDocument` refused as the client, Tab on a module and
on a document object's property, the history both ways, a paste, Interrupt
on a loop over `b.Width`, Ctrl+C on a typed line, a switch to the second
document and an object made there; the desktop sees both writes.
`tests/gui/sandbox-console-viewer-browser.py` (not registered; 17 PASS)
opens the served viewer page itself with `?console`, and
`scripts/console-drive.js` injects `web/viewerconsole.js` to drive the panel
the chrome mounted: the console says it is on the viewer's connection, reads
and writes the document, is refused a write while the owner has that one
client view-only and writes again when editing is given back, and follows
`fcviewerSwitchDoc` to the second document; the desktop side answers the
page's asks through the connection's roster label (which a view-only client
can still set), sees one client the whole run, and has exactly the writes
that were allowed.  C1's and C2's browser legs, re-run over the changed
bundle and driver (swiftshader now, for the viewer's WebGL): 11 and 15 PASS.

*Not in C4*: a local `ActiveView` facade -- the viewer exposes no camera
hooks to the DOM layer yet;
the macro echo; wrapping a statement's writes in an undo transaction (not
done, as the desktop console does not; open for the owner's view of a
client's edits); `input()`, which falls to pyodide's default stdin and is
untried; the latency and memory of C5.

**C5, BUILT 2026-09-15.**  What a console statement costs when the host
is a LAN or a tunnel away, and what the guest costs the page.  A statement
costs its bridge ops times the round trip, and a loop made one op per
element it touches: on a LAN the console was usable as built, through a
tunnel anything with a loop was too slow.  By C5's own criterion that
called for the snapshot op; what was built is a narrower one, the prefetch
of sibling reads (below), and a loop is now a few ops.

*The rig.*  This box has no second machine, no sudo for `tc netem` and no
`cloudflared`, so the round trip is injected: `scripts/delay-proxy.js`
relays TCP holding every chunk rtt/2 each way, in order.  Bridge ops are
strictly one at a time -- a guest waits on every answer -- so a fixed delay
is an honest model of what a LAN or a tunnel adds to them.  It models no
bandwidth, loss, jitter or TCP slow start, so its BOOT figures are not a
tunnel's.  `web/latency-test.html` boots the guest through it and runs
eleven statements a console user types against a document of 50 boxes and
Poly, a 100-edge polygon, three reps each (one at 30 ms and above);
`scripts/console-drive.js` answers the page's `window.fcxMark(label)` with
every browser process's PSS and RSS from /proc, and runs Chrome with
`--expose-gc` so the page collects before it marks.
`tests/gui/sandbox-latency-browser.py` (not registered) runs the page at 0
(no proxy), 2, 10, 30 and 100 ms: 74 PASS, every statement making the same
op count at every RTT.

*Measured* (headless Chrome 153, the host on loopback, best wall time in ms):

    statement                                    ops    0 ms    2 ms   10 ms   30 ms  100 ms
    -------------------------------------------  ---  ------  ------  ------  ------  ------
    doc.Name                                       2     1.1     6.5    22.9      69     214
    Box.Length                                     3     1.9     9.6    36.9      99     313
    Box.Length = 12                                3     1.7    10.2    37.4     101     312
    len(doc.Objects)                               2     2.1     7.5    24.1      71     208
    [o.Name for o in doc.Objects]                 52    24.3     158     608    1712    5391
    [o.Placement.Base.x for o in doc.Objects]     52    26.2     155     617    1709    5367
    Box.Shape.Volume                               4     2.2    13.2    46.3     131     416
    len(Box.Shape.Edges)                           4     2.1    13.1    48.0     137     414
    sum(e.Length for e in Box.Shape.Edges)        16     7.3    51.2     191     531    1659
    sum(e.Length for e in Poly.Shape.Edges)      104    45.5     312    1217    3358   10739
    [v.Point.y for v in Poly.Shape.Vertexes]     105    46.2     316    1223    3451   10914

Per op: 0.46 ms direct (0.41 of it waiting on the bridge, the guest's own
CPU about 0.05), then 3.0, 11.7, 32.8 and 103.6 ms -- the RTT, plus the
direct cost, plus the proxy's own timer lateness (0.6 to 3 ms).  An op is
about 25 bytes up and 100 down, so bandwidth does not matter to the bridge;
round trips are all of it.  What that means for a console:

- A property, a write, `len(doc.Objects)`, a shape's volume: 2 to 4 ops,
  under 50 ms on a LAN, 0.2 to 0.4 s at 100 ms.  Fine everywhere.
- A loop over the objects or over a shape's sub-elements: one op per
  element (`o.Name` a read per object, `e.Length` a get per edge,
  `v.Point.y` one per vertex -- the Vector comes by value).  Fifty objects:
  0.16 s at 2 ms, 1.7 s at 30 ms, 5.4 s at 100 ms.  A hundred edges: 0.3 s,
  3.4 s, 10.7 s.  A LAN is fine; a tunnel is not.
- Direct on loopback, the same loops take 25 to 46 ms.  The fixed cost is
  not 8.1's 5 us hop but 0.4 ms: the socket both ways, the hop to the GUI
  thread and back, the JSPI suspend and resume.  Not decomposed.  On a LAN
  it is a sixth of the per-op cost; through a tunnel it is noise.

*Memory* (PSS, the same at every RTT within 10 MB).  The page's renderer
process is 94 MB bare, 280 MB with the guest booted, 281 to 291 MB after
the bench: **the guest costs the page about +185 MB PSS** (+190 MB RSS).
Of that, 49.6 MB is the wasm heap and about 50 MB pyodide's JavaScript
heap after a collection (44 MB after the bench); the rest is compiled wasm
code and the runtime's buffers.  Chrome's other processes barely move:
browser 100 MB, GPU 100 to 115 MB, utility 55 to 68 MB (the network
service, +10 MB for the downloads), zygote 34 MB.  The desktop's 335 MB is
the whole V8 process, so +185 MB is the page's comparable figure.  A
phone's is NOT measured: there is none on this box, and Chrome's device
emulation changes the viewport, not the memory.

*Boot through the proxy*: runtime 1.25 to 1.46 s, wheels 0.38 to 0.62 s
from 0 to 100 ms -- but the proxy's local TCP has no slow start and no
bandwidth cap, so these understate a real tunnel's first boot of a 13 MB
runtime (cached after that).  Connect is one RTT plus 3 ms.

*The prefetch, BUILT 2026-09-15.*  Asked of the owner with four shapes --
a per-statement prefetch, an explicit batch call, a second guest on the
host for far clients, or nothing -- and **ruled: the prefetch per
statement**, whose trade is that a loop's values are as of the read that
brought them rather than live.  Built narrower than the sizing's snapshot:

- *What crosses* (FcxWire.h, `"pf"`).  A read_prop or get_attr off one
  element of a list the table handed out is answered, next to its `val`,
  with the same read of the elements after it: `"pf": [[id, value], ...]`.
  Only the member the guest actually read, only the siblings in that list,
  only values that cross by value -- a read answered by a handle
  prefetches nothing, and a sibling whose answer is one is dropped with its
  use given back, so the table mints nothing the guest did not ask for.
  32 at the first miss of that member in that list, twice as many at each
  miss after, at most 1024, cut at 20 ms of host time per reply: what a
  loop that stops early leaves unread is bounded by what it read.  The
  checks are the op's own, per sibling (`readAttribute`/`readProperty`,
  the two op bodies lifted out of the dispatcher); `checkGetattr` returns
  at once for a FreeCAD-bound object, so a speculative read queues no
  prompt.  A fixed-layout reply carrying `pf` goes as CBOR (kind 0).
- *The guest* (ImageBridge.cpp) keeps them keyed by handle id, op and
  member, and decodes each hit afresh -- a new value object every time,
  as natively, which the write-back tracking relies on.  It forgets them
  before any op that is not a pure read (a closed list: the reads, `len`,
  `get_item`, `bool`, `str`, `ext`, `resolve`, the app queries, `mod_get`,
  `lib.source`, `pkg.missing`, `release`), so a write, a call, a
  `mod_call` -- even a pure one -- makes the next read a hop; and at the
  start and end of every host request, so nothing outlives a statement.
- *Where.*  On for a remote guest's endpoint (SandboxRemote.cpp;
  `FC_SANDBOX_PREFETCH=0` turns it off, read when a connection's endpoint
  is made).  Off for the desktop's own guest, MEASURED 2026-09-15 on the
  user's question whether it should be on there too
  (`DISABLED_BenchPrefetchInProcess`, 1000 objects, mean us per
  evaluation, off / on):

      statement                                     off      on
      --------------------------------------------  -----  -----
      42 (the pack alone)                              29     28
      len(d.Objects) (the list alone)                6090   6243
      [o.Name for o in d.Objects]                    8719   8459
      sum(o.Width for o in d.Objects[1:])            8639   7915
      sum(e.Length for e in s.Edges), 1000 edges     9702   9314
      d.Objects[5].Name (32 siblings unread)         6849   6391
      two reads far apart (64 unread)               12188  12469
      stop at the 40th of 1000                       6294   6596
      s.Edges[0].Length                              7070   7530

  A loop of 1000 hops is about 2.6 ms, the prefetch saves 0.3 to 0.7 ms
  of it, and a statement that reads one element or stops early pays 0.15
  to 0.46 ms for siblings it never reads -- both inside the run-to-run
  noise (the one-read case moved 450 us between two runs).  The prefetch
  can save at most the hops, and on the desktop the hops are not the
  cost: MINTING the list is, 6 us a handle against 3 us a hop.  So it stays
  off there; `ImageHost::setPrefetch` exists so a test can run the guest's
  half in process.

*Where a handle's 6 us goes* (PROFILED 2026-09-15, on the user's ask; a
throwaway probe -- per-segment steady_clock timers on both sides, not
committed -- over `len(d.Objects)` on 1000 objects, 50 evaluations; the
guest's clock steps 285 ns, fine enough to sum):

    segment                                                 us per handle
    ------------------------------------------------------  -------------
    host: Document.Objects read                                 0.03
    host: encode -- the json map 0.46, facadeKeyFor 0.13,
          handleKey 0.13, table add 0.08, type checks 0.06,
          extension keys 0.03                                   0.92
    guest: the crossing (host dispatch + reply CBOR + copy)     1.48
    guest: reply CBOR -> nlohmann json                          1.25
    guest: json -> proxy, of which                              3.43
             setting _id/_ty/_fc/_k                             2.04
             the facade class lookup                            0.44
             instantiating                                      0.23
             the rest (field finds, strings, the key tuple)     0.72
    guest: dropping the list (1000 __del__, releases queued)    0.30
    total, host-timed                                           7.00

The one outlier is the slots: `HostHandle` defines `__setattr__` in Python
(the write_prop hook), and `decodeValue` sets each of the four slots with
`PyObject_SetAttrString`, so every handle runs four Python-level calls that
only end in `object.__setattr__`.  Setting the slot descriptors directly
(`PyObject_GenericSetAttr` with interned names) skips them; the class
lookup makes a key string and two dict lookups per handle and can be
cached per facade key.  The two JSON stages (host map 0.46, guest decode
1.25) are the next tier -- nlohmann allocates a map and its keys per handle
on both sides.

*The first tier, FIXED 2026-09-15* (ImageMarshal.cpp, guest only): the four
slots are set with `PyObject_GenericSetAttr` on interned names, straight
into `HostHandle`'s slot descriptors (every facade class declares
`__slots__ = ()`, so nothing else answers them); the facade class of the
last key is kept with a reference of its own; and the type name, facade key
and document name reuse the last str made for the same text.  Re-benched
(`DISABLED_BenchPrefetchInProcess`, prefetch off, mean per evaluation):

    statement                                  before   after
    -----------------------------------------  ------  ------
    len(d.Objects), 1000 objects               6.09 ms 4.42 ms
    [o.Name for o in d.Objects]                8.72 ms 7.38 ms
    sum(o.Width for o in d.Objects[1:])        8.64 ms 6.66 ms
    sum(e.Length for e in s.Edges), 1000 edges 9.70 ms 8.37 ms
    d.Objects[5].Name                          6.85 ms 4.40 ms

A handle is 4.4 us from 6.1.  The JSON stages are what is left of it.
Suites over the change: ExpressionImage*/ExpressionRouting*/
ExpressionSecurity* 121 OK, ctest 642/642, SandboxProgram 46 OK,
FeaturePythonChain 44 OK, C2's browser leg 15 PASS.

*Measured* with the prefetch (the same rig; with it off the figures are
the table above's within noise):

    statement                                    ops    0 ms    2 ms   10 ms   30 ms  100 ms
    -------------------------------------------  ---  ------  ------  ------  ------  ------
    doc.Name                                       2     1.2     6.2    23.1      68     213
    Box.Length = 12                                3     1.8    10.3    35.8      97     309
    [o.Name for o in doc.Objects]                  4     3.0    13.9    47.8     133     416
    [o.Placement.Base.x for o in doc.Objects]      4     3.4    15.5    48.6     135     417
    len(Box.Shape.Edges)                           4     2.3    12.8    46.2     132     417
    sum(e.Length for e in Box.Shape.Edges)         5     3.0    16.4    57.4     161     520
    sum(e.Length for e in Poly.Shape.Edges)        7     4.8    23.4    82.1     232     734
    [v.Point.y for v in Poly.Shape.Vertexes]       7     5.4    24.2    85.6     236     721

Fifty objects through a 100 ms tunnel: 5.4 s -> 0.42 s; a hundred edges:
10.8 s -> 0.73 s; and direct, 24 ms -> 3 ms.  What is left is a CHAIN:
`doc.getObject('Box').Shape.Edges` is three reads on three different
objects, one op each, which no sibling prefetch reaches -- a statement is
3 to 7 round trips, 0.2 to 0.7 s at 100 ms.  Not addressed: batching a
chain needs the guest to say what it will read next.

*Gates*: `ExpressionImageEvalTest.prefetchAnswersSiblingReads` (registered,
in process: 41 names are 42 get_attr hops off and 3 on, 40 properties 40
and 2, a write inside the statement seen by the read after it, a hit a
fresh object, `o.Document` -- a handle -- prefetching nothing);
`GuiSandboxBridgeServe` 38 PASS (+5: the first 32 siblings with their
values, the doubling, a fixed-layout read answered as CBOR, a handle member
bringing none); `tests/gui/sandbox-latency-browser.py` now runs every RTT
with the prefetch on and off, 147 PASS, the op counts the same at every RTT
of a mode.  Re-run over the changed guest: C2's and C4's browser legs 15
and 24 PASS; ExpressionImage*/ExpressionRouting* 99 OK, ctest 642/642,
SandboxProgram 46 OK, FeaturePythonChain 44 OK.

*Not in C5*: a real LAN or tunnel -- the proxy stands in for both, and a
Cloudflare quick tunnel would publish the served document on the internet,
which is not done without the owner's say; a phone; the decomposition of
the 0.4 ms loopback cost; batching a chain of reads.

### 7.21 The proxy chain: document programs extend native objects **[planned and RULED 2026-09-12, see docs/ProxyChain.md; P0 and P1 BUILT 2026-09-12 -- the hook refactor, then `ProxyExp` and the App-side chain; P2 BUILT 2026-09-13 -- `ViewProxyExp` and the view-side chain; 7.17 RE-SIZED against it 2026-09-13, and ProxyChain.md 4.5 records what P1 does not deliver, RULED and BUILT 2026-09-13 (4.6); P3, the sandbox, BUILT 2026-09-13 inside 7.17's D2]**

The user's answer to 7.17's gap against the spreadsheet-as-object
model (2026-09-11: cells as attributes and methods, aliases as the
interface, a copy as an instance, typing missing): extend
`FeaturePythonT` / `ViewProviderFeaturePythonT` with a plain
`PropertyXLinkList`, `ProxyExp`, whose linked objects are asked at
runtime for overriding methods by name and signature
(`expExecute(self, obj)`, the linked object as `self`, the feature as
`obj`), a "not handled" return passing to the next link and finally
to the real `Proxy` -- multiple inheritance by chain, agnostic to what
the linked object is (a sheet whose alias'd cells are callables, a
Python-scripted object, a library, a guest stand-in; static or at
runtime).  Two lists, both on the App object: `ProxyExp` for the
App hooks (a dependency, Global scope) and `ViewProxyExp` for the
view hooks (Hidden scope, Prop_NoRecompute, read by the view provider
through updateData).  The per-hook bodies collapse onto one C++
template; cog generates the hook table only, at build time, the first
`generate_from_cog` user.  The plan, the stages P0-P3, the rulings,
and what it changes in 7.17 (D1 stands; D2's carrier is one of
several; the typed-sheet discussion is subsumed: `ProxyExp` is the
type link, the chain is the delegation) are in docs/ProxyChain.md.  The variant Link idea recorded the same day is
docs/VariantLink.md, a parallel thread for later.

### 7.22 G7 sized: the desktop's panels in the browser, a DOM view over the widget layer **[sized 2026-09-16; the five questions RULED 2026-09-16; W1's host half -- the origin echo -- BUILT 2026-09-16; W1 BUILT and PROVEN on screen 2026-09-16]**

The question, asked with 7.19's mirror complete as sized (M1-M3 built,
M4 measured 2026-09-11): the desktop's real task panels are already
walked into models and streamed to any subscriber, and nothing in the
browser consumes them.  What does that consumer cost.  Sized here;
**NOT built, and no TypeScript written -- the user ruled 2026-09-16
that the sizing comes first and stops for review**, because 7.12's
1.5-2k of TypeScript is a multi-session build rather than a bounded
item.

**The short answer.**  Everything the browser needs is on the wire and
gated on the host already; the missing piece is one consumer of about
2.2-3.3k of TypeScript and CSS in `src/Gui/Renderer/web`, in five
stages, of which the first -- a Pad-class form panel, editable, in the
page -- is one to two sessions.  The host needs exactly one change, and
it is 20 lines: today a client is never told how the host CORRECTED its
own write ("The one gap on the host side" below).  One design choice
needs the user before W1 starts (question 1).

**What is already built and reused as is.**

- *The panels are on the wire.*  7.19's `PanelMirror` walks the real
  `TaskBox` tree into store models and streams them; M2 reflects item
  views and sends pictures and icons as image ids; M3 mirrors top-level
  dialogs as `dialog:<n>` roots.  No workbench was edited for any of it,
  and none has to be edited for this.
- *The ops exist and are gated.*  `src/Gui/SceneWidgets.cpp` registers
  `widgets.subscribe` / `unsubscribe` / `icon` / `image` / `update` /
  `custom` on the scene socket's control lane, behind the same door as
  every other control op (`registerSceneControlOp`, the write ops with
  the write flag, so a view-only connection is refused).
- *The class set is a build artifact.*  `widget-models.json`
  (`build/.../share/Pyodide/`, from `src/Tools/bindings/
  dumpWidgetModels.py`, gate `SandboxModelDump.py`) carries 39 model
  classes and 54 Qt class names: `base`, `properties` with type,
  default and `allowNone`, `signals`, and `qtClasses` mapping
  `Gui::PrefQuantitySpinBox` -> `QuantitySpinBoxModel`.  The DOM side
  reads one file and needs no Python.
- *The browser chrome is a working DOM layer.*  `control.ts` already
  correlates ops by id and delivers uncorrelated frames to `onPush(op)`
  subscribers -- a `widgets` push needs no new transport, no second
  socket and no change to `main.cpp`, which re-dispatches every control
  frame as an `fc:control` event.  `panel.ts` has the floating-card
  behaviour (drag with pointer capture, remembered position, the
  `NARROW` 640 px bottom-sheet switch); `console.tsx` (7.20 C4) is the
  precedent for a panel that talks to the host over this socket; the
  bundle is wired into CMake as `FCVIEWER_UI` (vite, `npm ci`, the
  rollup-wasm fallback) and `shell.html` loads it as `web/inspector.js`.
- *The cost is measured* (8.4).  A Pad panel opens as 64 messages /
  45.9 KB / 61 models, OrthoArray as 48 / 35.1 KB / 46, Sketcher's as
  31 / 29.7 KB / 29; a panel at rest sends nothing, a repaint burst
  sends nothing, ten keystrokes cost 20 messages / 1.5 KB.  The one hot
  spot is Sketcher's refill on a solve: 162 messages / 21 KB, three ops
  a row.

**What the walker consumes.**  Exact, from the code; this does not need
re-deriving.

- *Subscribing.*  `widgets.subscribe {panels|toolbars|all}` replies
  `{ok, subscribed, panel, dialogs, theme, locale}` -- `panel` is the
  id of the task panel up right now or null, so a client tells "no
  panel" from "not yet", `dialogs` the top-level dialogs in show order,
  `theme` the host's icon override and `locale` its `QLocale::name()`.
  The snapshot does NOT ride the reply: it follows on a zero timer, so
  the client must accept `open`s arriving after it.  The mirror starts
  with the first `panels` subscriber and stops with the last.
- *Frame shapes.*  Every pushed frame is `{op:"widgets", method, id,
  ...}`.  `open` SPLICES the snapshot at top level (`model`, `qtClass`,
  `state`, `layout?`, `items?`, `parent?`); **every other method nests
  its payload under `content`**.  The vocabulary is exactly five:
  `open`, `close`, `state`, `update`, `custom`.  So the client needs a
  method dispatcher beside the snapshot applier, not one shared path.
- *The snapshot.*  `model` keys `classes` in the dump, `qtClass` keys
  `qtClasses`; every state key carries the `q_` prefix; an object-valued
  property crosses as the string `IPY_MODEL_<id>`, and the ref walk
  RECURSES through lists and maps, so a ref sits at any depth.
- *Arrival order.*  `snapshotOrder()` visits what an object refers to
  first and deliberately EXCLUDES `parent` ("a container names its
  children through its layout").  **Children arrive before their
  container**: the walker builds bottom-up and needs no buffering.  Do
  not assume parents-first.
- *Layouts.*  `Layout::spec()` -> `{class, name, items[], margins?,
  spacing?, +extras}`.  Each item is exactly one of `widget` (a ref) |
  `layout` (nested, recursive) | `action` | `separator` | `stretch` |
  `spacing` | `spacer [w, h, hPolicy, vPolicy]`, plus optional `pos`
  (grid coordinates, with a row span in the third slot), `stretch` and
  `align` on a widget or nested layout.  The recursion and the
  spacer/stretch/policy handling are where 7.12's 1.5-2k goes.
- *Item views.*  `ItemView::snapshot()` is a list of rows; a row is
  `{id, cells[], children[] (recursive), expanded?, hidden?, flags?}`
  and a cell is `{text?, icon?, toolTip?, statusTip?, whatsThis?,
  check?, flags?, fg?, bg?, bold?, align?}`.  Live changes arrive as
  `custom` with an `item` op, in the same shape a client writes.
- *Pictures and icons.*  Both travel as `img:<sha1>` ids in the bag --
  a custom-painted leaf's pixmap, a button's icon, a cell's icon -- and
  are fetched once with `widgets.image {name}` (PNG, base64, with width
  and height).  A named theme icon is `widgets.icon {name, size}`,
  answered as SVG text or a base64 PNG.  Both are content-addressed, so
  a page-lifetime cache never goes stale.
- *Writing back.*  `widgets.update {target, state}` with `q_` keys ->
  `Store::applyUpdate`; `widgets.custom {target, content}` ->
  `applyCustom`.  Both mutate a real widget through the panel's own
  slots, so both are refused on a view-only connection.
- *Roots.*  `PanelMirror::owns` answers for the list id, `panel:<n>`
  (the task panel root), `pw:<n>` (a mirrored picture widget) and
  `dialog:<n>` (a top-level dialog).  Tool bar ids are
  `ToolBarMirror`'s and belong to the `toolbars` subscription, which
  this section does not touch.

**The one gap on the host side.**  `SceneWidgetStream::onMessage` fans
a frame out to every subscriber EXCEPT its origin (`client == origin`),
and `Store::applyUpdate` runs the client's write inside an
`OriginScope` holding that client's id.  The property change the write
provokes is announced on the same stack, still stamped with that
origin -- so **the writer never hears what the host made of its write**.
That is right for the echo of an unchanged value and wrong for every
correction: a spin box clamping to its maximum, a quantity re-parsed
into its display unit, a slot that writes the field back.  The client
then shows a value the document does not have, and nothing corrects it
until another client touches the same widget.

Two answers.  (a) The client stays optimistic and accepts the drift;
(b) the host sends the announced state to the origin as well WHEN the
applied value differs from what that client wrote -- `applyUpdate`
already holds both halves, so it is a comparison and a targeted send,
about 20 lines, and the wire shape does not change.  **Recommended:
(b), in W1**, because (a) is undetectable from the browser and the
first field anyone tests is a quantity.  It is the only C++ this
sizing asks for.

**BUILT 2026-09-16**, as ruled.  `Store::messageTo(client, ...)` beside
`message`, emitted by `applyUpdate` AFTER the `OriginScope` closes -- so
nothing it sends is stamped with the writer -- carrying only the keys
that diverged; `SceneWidgetStream::onMessageTo` sends them to that one
client, if it is still subscribed and still wants that id.  The
comparison is two-sided on purpose: the WIRE forms first, which settles
a property whose value is an object (a ref either way), and then
`Widget::valueDiffers`, which coerces by the DECLARED type -- without
that second half an int that arrived as a JSON double would be reported
as a correction of itself.  The helper is new because `coerce` is
protected and the knowledge of a bag key's type belongs on the widget.
Gate: `test_widgetStream` gains the case -- a slot writes the field
back, the writer gets exactly one targeted `update` carrying the
corrected value, and a write the host leaves alone sends it nothing;
`FormWidgets_Tests_run` 22 passed, 0 failed, the three panel-mirror
cases among them.  One limit kept deliberately: `applyCustom` is not
echoed, so a correction a client's EVENT provokes stays invisible to
it -- no client sends events yet, and W2 is where that would change.

**Stages and gates.**

    W1  the spine and the form panel.  The widgets client (subscribe
        over the existing lane, the model store, ref resolution at
        depth, the five methods, the `q_` strip), the layout walker
        (VBox/HBox as flex, Grid as CSS grid with `pos` and spans,
        Form as a two-column grid, margins/spacing/stretch/align/
        spacer/separator), the panel container (the `panel:<n>` root,
        TaskBox headers, the dialog button box, close), and the form
        leaves: label, line edit, quantity/double/int spin boxes,
        check box, radio, combo, push and tool buttons, group box,
        frame, stacked and tab widgets, scroll area, splitter.  Write
        -back on the edit signals, and the origin echo above.
        Gate: the replay gate below over Pad and OrthoArray fixtures
        (every model realized, the layout plan matching the .ui's
        structure, a `setText` from the host applied, a client edit
        producing the right `widgets.update`), plus the hand-opened
        page against a live serving FreeCAD.
    W2  item views.  Rows, cells, nested children, expanded/hidden/
        flags, the column headers, check writes, selection, and the
        `custom` item ops applied as a batch per frame rather than per
        op -- 8.4's Sketcher refill is 162 ops in one solve and must
        not be 162 reflows.  Gate: the Sketcher fixture replays to the
        right row tree; a check write produces the op the host expects.
    W3  pictures, icons, theme, locale.  `img:` and `widgets.icon`
        with a page-lifetime cache, the picture leaf as an image with
        M3's mouse replay, the theme from the subscribe reply, numbers
        and dates through the reported locale.
        Gate: a fixture carrying a QSvgWidget and a button icon; the
        image op is asked once per distinct id.
    W4  dialogs and modality.  `dialog:<n>` as a modal layer over the
        panel, the button box's exec code returned, the QMessageBox
        shape, and the file chooser model routed to the client's own
        picker rather than the host's.  Gate: M3's QMessageBox fixture;
        a button click returns the exec code.
    W5  the measurement and the finish.  First paint of a panel open
        against 8.4's host-side numbers, apply time per burst, the
        keystroke round trip; the narrow layout (bottom sheet), touch
        targets, dark and light, and what a panel does when the socket
        drops.

**The gate, and why it is a replay.**  There is no browser CI on this
box: the host-side gates run under Xvfb through
`scripts/sandbox-gui-gate.py`, and the browser-side artifacts so far
(`sandbox-test.html`, `bridge-test.html`, ...) are pages a person
opens.  A DOM view of a panel deserves better than that, and the wire
makes it cheap: extend the existing `SandboxPanelMirror.py` gate with a
dump mode that writes the exact JSON frames of a Pad, OrthoArray,
Sketcher and QMessageBox session into fixture files, then gate the
walker in node by replaying each fixture and asserting the result.
Keeping the walker's model store and layout plan PURE (frames in, a
view plan out; the DOM built from the plan) makes that assertion a data
comparison and adds no dependency -- the alternative, rendering Solid
into jsdom or happy-dom, buys a truer test for a new devDependency and
a slower gate.  **Recommended: the pure plan plus one DOM smoke check
in the hand-opened page** (question 5).

**The fixture corpus BUILT 2026-09-16.**  `SandboxPanelMirror.py` grows
a recorder (`_Recorder`, armed only by `SANDBOX_PANEL_FIXTURES`): a
proxy over `FormWidgets` keeping every frame `pushed()` drains and every
`widgets.subscribe` reply, written per case in `tearDown`.  A proxy
rather than a patch, so not one assertion in the gate changed.  Six
fixtures, one per stage, in `src/Gui/Renderer/web/src/widgets/fixtures/`:

    pad_panel             69 frames   64 open, 4 update, 1 close    W1
    draft_orthoarray      52          49 open, 2 update, 1 close    W1
    cam_op_panel          95          92 open, 2 update, 1 close    W1
    sketcher_constraints  56          32 open, 21 custom, 2, 1      W2
    svg_picture           12           8 open, 3 update, 1 close    W3
    nested_messagebox     21          15 open, 4 update, 2 close    W4

Pad's 64 opens are 8.4's measured 64 messages -- the corpus checking
itself against the measurement.  They carry no absolute path, no home
and no temp directory; `locale` is `C` and `theme` empty under the gate.
Regenerate with `SANDBOX_PANEL_FIXTURES=<dir>` and
`SANDBOX_GUI_GATE_MODULES=SandboxPanelMirror` -- that module alone: a
root's id is a process-wide serial (`panel:1` to `panel:6`, the cases in
alphabetical order), so a different module set renumbers them, which a
replay does not care about (it is self-consistent) but a diff does.

**The corpus earned itself at once: an `open` arrives TWICE for the same
id.**  Every one of the six starts with two adjacent, byte-equal opens
of `panel`, the mirror's list container.  `widgets.subscribe` calls
`checkMirror()`, which runs `PanelMirror::start()` synchronously; that
adopts the list with the announce on, and the client is already in
`_panels`, so it hears that open live -- then the deferred
`pushSnapshot()` sends an open for every object in the store, the list
among them.  Only an object adopted inside that window doubles, which is
why it is exactly one.  So **the walker's `open` must be an idempotent
replace**, which is what `Store::insert` does on the host.  Nothing to
fix on the wire: a rule to build to, and one no hand-written sample
would ever have shown.

**The harness runs on node 24** (`emsdk-5.0.3/node/24.19.0_64bit`, the
only node on this box and what `FCVIEWER_NODE` already points at), whose
`process.features.typescript` is `strip` -- it imports the walker's
`.ts` core directly, so the ruled pure-plan gate needs no compile step
and no new dependency.

**W1's core and its gate BUILT 2026-09-16.**
`src/Gui/Renderer/web/src/widgets/protocol.ts` is the reduction: ref
resolution at any depth, the `q_` strip, `open`/`close`/`state`/
`update`/`custom`, the item ops (`clear`, `insert`, `set`) and `roots()`
for the models no layout names.  Pure -- no DOM, no Solid, no fetch --
which is what lets `gate.ts` replay the six fixtures in node and assert
the walker's promises: every frame applied, no patch to an unknown
model, the subscribe reply usable as boot state, children before their
container, and an `open` for a live id tolerated as a replace.
Sketcher's 21 item ops leave 4 rows reading `Line`, `Edge1`, `g1`.
`npm run gate` -- ALL GREEN, the exit code the verdict; `npm run
typecheck` covers the core and is clean.  `tsconfig` excludes the gate
script alone (node builtins, and the `.ts` specifier the stripper wants,
neither of which the bundle's config describes); adding `@types/node`
for one script would have spent the dependency question 5 withheld, and
running it is the check.

**Three things the recorded frames corrected in this sizing**, each
found by reading the corpus rather than the C++: a grid `pos` comes
BOTH four wide and two wide -- `Layout::addWidget` writes
`[row, column, rowSpan, columnSpan]` and `Layout::addRow`, a form's
row, writes `[row, column]` alone; 145 of the corpus's 153 are four
wide, 8 are two, and 6 of those are in Pad's own panel -- so the spans
must default to 1 or a form row plans as `NaN` and the panel collapses
silently (this section claimed FOUR wide when the corpus first landed,
which was half right; the layout pass counted them and corrected it);
an `update` can carry a rebuilt `layoutSpec` beside the state keys, so
the update path needs a layout branch; and a picture arrives as a
`QLabelModel` whose `qtClass` is `QSvgWidget`, so the view keys off
`model` but must consult `qtClass`.

**The layout plan BUILT 2026-09-16.**  `layout.ts` turns a spec into what
a view places: the four classes the corpus carries (`QVBoxLayout` 53,
`QHBoxLayout` 24, `QGridLayout` 25, `QFormLayout` 2) onto stacks, a grid
and a form; the seven item shapes; the spans defaulted; the grid extent
computed once, so a view sizes its track lists without a second pass.  An
unknown class falls back to a vertical stack rather than throwing, and
the gate reports the class -- one unfamiliar container should render, not
blank the panel.  The gate grew four assertions a fixture (the class
known, the spans usable, the item shapes complete, a plan produced) and
stands at **62 checks, ALL GREEN**, with `npm run typecheck` clean.  The
core's `.ts` specifiers, which node's stripper needs literally, are
covered by `allowImportingTsExtensions`; only the gate script stays out
of the typecheck, for node builtins alone.

**A limit, stated rather than counted as covered:** `separator`,
`stretch` and `spacing` ITEMS never appear in this corpus -- they are
tool-bar shapes, and the tool bars are the other subscription -- so they
are planned but ungated.  What the corpus does carry is widget 250,
nested layout 24, spacer 9, and no layout extras at all.

**W1's client and views BUILT 2026-09-16 -- W1 is code-complete.**
`client.ts` is the socket half: it registers the push handler BEFORE
subscribing (the snapshot follows the reply rather than riding it, so
registering after drops the first opens), carries the write back, and
caches the content-addressed images and icons.  `panel.tsx` is the
views: the floating card on the chrome's own drag and bottom-sheet
behaviour, a plan rendered as flex or CSS grid, and the leaves the
corpus ranks (`QLabel` 66, `QPushButton` 27, `QuantitySpinBox` 27,
`QWidget` 26, `QCheckBox` 25, `QGroupBox` 22 -- 10 of them `TaskBox`
headers -- `QComboBox` 14, `InputField` 9, `QToolButton` 8,
`QDialogButtonBox` 7, `QDialog` 7).  A class with no view yet still
renders its layout, so an unfamiliar widget costs its own box and not
the panel.  `main.tsx` mounts the card beside the console and the sheet
with a launcher entry; `style.css` gains the `.fc-panel` block in the
material those cards already use.  The origin echo needed no client code
at all, which was the point of building it host-side.

**What is verified, and what is not.**  `npm run typecheck` is clean,
the replay gate is 62 checks ALL GREEN, and `npm run build` emits the
card into the chrome (`panel.js`, 14.2 kB, 5.8 kB gzipped).  That is
compilation and reduction, **not rendering**: the DOM check question 5
ruled -- the hand-opened page against a serving FreeCAD -- has NOT been
run.  W1 is code-complete and unproven on screen, which is the honest
state of it.

**W1 PROVEN on screen 2026-09-16.**  `scripts/demo-taskpanel.py` through
`renderer-serve.sh` (Pad's own C++ dialog up on a headless serve), the
page opened with `?panel` -- a flag added for the reason `?sheet` has
one, that a headless run cannot reach the launcher -- and driven by
`scripts/panel-drive.js`: it reports the card's shape as JSON (title,
TaskBox headings, labels, fields, combos, checks, buttons, rows, and the
computed grid tracks), optionally types into the first field and reports
what the host sent back, and writes a screenshot.  The result: Pad's
panel drawn -- 4 headings, 13 labels, 8 fields, 5 combos carrying their
items, 12 check boxes, the Profile list holding `SketchPad`, OK/Cancel,
and grid tracks that are real px rather than the `NaN` the span default
was guarding against.  Typing 25 into Length leaves the host holding
`lengthEdit` `rawValue` 25 / `"25.00"` and the pad visibly taller in the
same screenshot: the write path, end to end, in a picture.

**Two things the screen found that no fixture could.**  *The card
subscribed once, at open* -- and `control.ts` refuses an op on a socket
not yet up with `Offline` rather than queueing it, while the WASM module
installs the uplink seconds into the load.  So the card `?panel` opens
during page load asked too early, took the refusal as final, and said
the stream was unavailable until a reload.  An `Offline` is "not yet",
not "no", which is the rule `sheet.tsx` already keeps; the subscribe is
retried while the card is open.  *An icon-only tool button drew its
whole tooltip as its label* -- Pad's is "Temporary clear link references
for new selection" -- which ran the Profile row off the card and over
its neighbours.  The icon is W3; until then the label is a placeholder,
the sentence stays on the title, and a button clamps to its cell so no
desktop label can do that again.  Both are the fixtures' blind spot by
construction: one is about the socket's timing and the other about
pixels, and the replay gate has neither.

Then W2 to W5 as staged: the item views properly (the checks, the
nesting, the refill coalesced), the pictures and icons, the dialogs and
modality, and the measurement against 8.4.

**Cost** (new; TypeScript unless noted):

    the client: subscribe, frame dispatch, model store, refs,
      the write path                                            250-350
    the class set: the leaf views that matter (about 25 of 39)  500-700
    the layout walker: four kinds, pos/span/stretch/align,
      spacers, separators, margins                              250-350
    the panel container: roots, TaskBox headers, button box,
      open/close, the socket's comings and goings               200-300
    item views (W2)                                             300-400
    images, icons, theme, locale (W3)                           150-250
    dialogs, modality, the file chooser (W4)                    150-250
    CSS                                                         200-300
    the gate: the fixture dump (Python, in the existing gate)
      and the node replay harness                               250-350
    the host's origin echo (C++)                                 ~20
    total                                                       2.2-3.3k

7.12's 1.5-2k stands for the walker proper (the client, the class set
and the layout walker are 1.0-1.4k of it); the rest is the container,
the gate and the CSS, which that number never covered.  Rough scale at
sec 7's pace: W1 one to two sessions, W2 one, W3 and W4 one together,
W5 one.

**Decided here: borrow the shape of ipywidgets, not the code.**  The
wire is ipywidgets-SHAPED -- `IPY_MODEL_` refs, a state prefix, open/
close/state/update/custom -- which is worth asking about before writing
2k of TypeScript, and the answer is no.  There is no kernel and no
Jupyter message envelope here (frames ride the scene socket's control
lane); `@jupyter-widgets/base` brings a Backbone-era model layer and a
manager that expects comm objects from a kernel connection; and the
class set it renders is ipywidgets', while ours is Qt's -- line edits,
group boxes, stacked widgets, grid layouts with spans, item views.  The
overlap is the ref resolution and the state diff, a couple of hundred
lines we write anyway; the 1.5-2k has no counterpart to borrow.  A
Backbone dependency against a 7.5 KB Solid bundle is also the wrong
trade for the mobile tier.  What we keep from the resemblance is the
vocabulary, which is already on the wire.

**Limits, stated.**  A Qt desktop host only -- a headless serving
FreeCAD has no panels to mirror, and a Qt-free panel backend is 7.12's
H2/H3, still optional.  One shared session (8.11): a client's write is
the desktop user's write, and a modal blocks every client.  No
per-client grant; `Preferences/Fw/PanelMirror` is the only gate, and a
per-client one is 8.12's multi-user work.  Custom-painted widgets
arrive as pictures, not as widgets.  `styleSheet` and `font` travel in
the bag but are Qt spellings, not CSS, and W1 ignores both.  No drag
and drop in item views.  The DOM panel will not look like the Qt panel
(question 1).

**Questions for the review -- ALL FIVE RULED 2026-09-16.**

1. *The look.*  Chrome-flavoured (the inspector's language, ThinClient
   4.3) or as close to the desktop's Qt panel as DOM can get.
   **RULED: chrome-flavoured** -- the viewer already speaks it, a
   near-miss of a Qt panel reads as broken rather than familiar, and
   the desktop's QSS does not travel anyway.
2. *The origin echo.*  Take the 20-line host change in W1, or leave the
   client optimistic.  **RULED: taken, in W1.**  It is the only C++
   this section asks for.
3. *W4's scope.*  Dialogs and modality inside G7, or deferred until the
   form panel has been used.  **RULED: inside G7, as staged** (stage 4
   of 5).  A client that ignored the `dialog:<n>` roots would silently
   swallow a `QMessageBox` a panel slot raised -- the user clicks and
   nothing happens -- which is worse than not mirroring panels at all.
4. *The panel's placement.*  A floating card like the console, or a
   docked side rail on a wide viewport and a bottom sheet on a narrow
   one.  **RULED: floating** (the user's call, against the
   recommendation).  It is also the cheaper half: `panel.ts` already
   carries the drag with pointer capture, the remembered position and
   the `NARROW` fallback, and `console.tsx` is the working precedent
   for a floating panel that talks to the host over this socket, so
   the container reuses behaviour rather than writing a rail.
5. *The gate shape.*  The pure view plan with no new dependency, or
   jsdom/happy-dom for a truer DOM assertion.  **RULED: the pure
   plan** -- the model store and the layout plan stay pure functions,
   and the DOM is checked by the hand-opened page.

### 7.23 Completion in the browser sized: a service, not a mirrored popup **[sized 2026-09-17; the phone and the dot trigger RULED 2026-09-17]**

The question, asked once W1 drew a real panel: a mirrored field is a
DOM input, and the desktop's completion is a `QCompleter` popup --
a `Qt::Popup` `QListView`, not a `QDialog`, not a child of the panel.
`PanelMirror::owns` answers for `panel:<n>`, `pw:<n>` and `dialog:<n>`
only, so **completion is structurally invisible to the browser today**,
and stays invisible after W4.

**What the code says, and it changes the shape of the answer.**  A
`Gui::QuantitySpinBox` -- Pad's `lengthEdit`, the field anyone would
actually type in -- has NO completer: `ExpressionSpinBox` takes
`spinbox->findChild<QLineEdit*>()` and only hangs the f(x)
`ExpressionLabel` on it (`SpinBox.cpp:51`).  Completion there lives in
`DlgExpressionInput`, a modal `QDialog` whose `ExpressionTextEdit` owns
the `ExpressionCompleter`.  The fields that own one directly --
`Gui::InputField`, `ExpressionLineEdit` -- appear in almost no task
panel (`Spreadsheet`'s dialogs, `DlgPropertyLink`).  So "serve the
widget's own completer" would light up nothing in the panels a phone
user opens.  Two consequences, both ruled here:

- The service must be able to build a completer **for a bound field
  that has none**, from `Fw::ExpressionBound::boundPath()`'s document
  object -- which is exactly what the f(x) dialog would have done.
- The card gets **a dialog of its own**, in the page, with the parts
  `DlgExpressionInput` has: a multi-line editor, a live result pane,
  and OK / Discard.  **CORRECTED 2026-09-17** -- this first read
  "an inline editor in the field's row", which was built and shown to
  the user, who ruled against it from the screenshot: expression entry
  is supposed to be a dialog for entering an expression, and editing
  in the spin box also throws away the result preview, which is half
  of what makes the desktop's dialog usable.
  What stands from the original reasoning is only which dialog: OURS,
  drawn in the browser, not the host's raised over the desktop user's
  screen.  `DlgExpressionInput` is modal on the host, and a single
  shared session (8.11) means raising it would freeze whoever is
  sitting at the desktop.  W4 still mirrors dialogs; this does not go
  through them.

**The op.**  One request/reply on the control lane, registered beside
`widgets.icon` with the write flag `false` (it reads names; a view-only
client may complete, and still may not write):

    widgets.complete {target, text, pos}
      -> {items: [...], details: [...], start, end}

`start`/`end` are the tokenizer's prefix range in the ORIGINAL text, so
the client splices `text.slice(0, start) + item + text.slice(end)` --
the same replacement `ExpressionLineEdit::slotCompleteText` performs.

**Never the desktop's own completer.**  `setCompletionPrefix` and the
tokenizer are mutable state, and the desktop's caret is not ours: a
browser query against the widget's live completer would corrupt what
the desktop user sees mid-keystroke.  The op builds its OWN
`ExpressionCompleter` on the bound object, per request, and asks that.
Per request rather than cached on purpose: the model a completer holds
is the document's object and property tree, and a cached one goes stale
the moment an object is added, renamed or deleted -- which is what a
completion is FOR.  The model is lazy, so the cost is the query; cache
it if a measurement says to, not before.  That also settles the popup: `ExpressionCompleter::slotUpdate` ends in
`showPopup`, which is why the entry point is a new
`ExpressionCompleter::complete(text, pos, start, end, details)` that
tokenizes, sets the prefix, harvests the rows through `setCurrentRow` +
`currentCompletion` (so `pathFromIndex` applies, exactly as activation
would) and **never pops anything**.  `slotUpdate` is untouched.

**The trigger: the dot, ruled.**  Completion fires when the user types
`.`, and then filters locally as more characters arrive -- one round
trip per dotted segment rather than one per keystroke, which is what
makes this usable over a phone's network.  Three qualifications the
code forces:

- **A dot after a digit is a decimal point, not a trigger.**  `10.` in
  a quantity field is a number; firing there would pop a menu over the
  keyboard every time someone types a length.  The rule is local and
  cheap (look at the character before the dot), and the host is the
  authority anyway: a query that matches nothing shows nothing.
- **A first segment has no dot**, so `>= 2` word characters also arm
  it, debounced -- `CommandCompleter` already refuses to fire under 3
  characters, so this is the house rule, not a new one.
- **An explicit ask** always works: a chevron in the field's row (a
  tap target, which a phone needs and a keyboard shortcut is not) and
  Ctrl+Space on the desktop.

Between dots the client filters the answered set itself, and re-asks
when local filtering empties out -- so a stale set can never strand the
user on a wrong answer.

**The phone, ruled.**  The card is already a bottom sheet under 640px
(`NARROW`), which is where the on-screen keyboard is.  A dropdown under
the field would be behind it.  So:

- The suggestions are a **horizontal chip strip** pinned to the bottom
  of the VISUAL viewport (`window.visualViewport`, which nothing in
  this chrome uses yet) -- the QuickType position, above the keyboard,
  reachable with a thumb.  On a wide viewport the same list renders as
  an ordinary dropdown under the field.
- **The tap must not blur the field.**  `preventDefault` on
  `pointerdown` over a chip, or the keyboard collapses, the viewport
  resizes, and the chip moves out from under the finger -- the classic
  version of this bug.
- **The keyboard must be able to type an identifier.**  A quantity
  field wants `inputmode="decimal"`, on which a phone offers no
  letters; the field switches to `inputmode="text"` as soon as its
  value starts with `=` (the expression lead char) so `Pad.Length` can
  be typed at all.  With `autocapitalize`, `autocorrect` and
  `spellcheck` off, or the phone helpfully capitalises identifiers.
- Chips are >= 44px of touch target, Enter accepts the highlighted one
  while the strip is open and commits the field when it is not, and a
  tap outside dismisses.

**Writing an expression back.**  Nothing today routes a written
`q_expression` to `Fw::ExpressionBound::setExpressionText` (only the
gate calls it), and wiring it into the property path would re-enter
`syncExpression`, which writes that same key.  So the inline editor
gets its own op, write-gated, whose reply can carry the parse error the
property path has nowhere to put:

    widgets.expression {target, text} -> {ok} | {ok:false, error}

**Stages.**

    A1  the host: ExpressionCompleter::complete(), the per-object
        completer cache, widgets.complete, widgets.expression.
        Gate: FormWidgets' widgetStream case -- a bound spin box
        answers "Pad." with Length among the items and a usable
        [start, end), an unbound plain edit answers nothing, and a
        view-only client may complete but may not set an expression.
    A2  the client: the completion controller (dot rule, local
        filtering, explicit ask), the chip strip and the dropdown,
        the inline expression editor on a bound field.
        Gate: the node replay gate over a recorded fixture for the
        pure half (trigger decisions and splicing are pure functions),
        then panel-drive.js typing into Pad's Length on a live serve.
    A3  the phone: visualViewport anchoring, the inputmode switch, the
        touch targets, and what the strip does when the keyboard
        closes under it.  Gate: panel-drive.js in a phone viewport
        with touch emulation.

**A1, A2 and A3 BUILT 2026-09-17, and proven on a live serve.**

The host: `ExpressionCompleter::completionsFor()` (tokenize, set the
prefix, harvest through `setCurrentRow` / `currentCompletion` so
`pathFromIndex` applies, and raise nothing), `widgets.complete` (not
mutating), and `widgets.expression` with a `preview` mode that parses,
validates and evaluates under a "session" scope with function calls
DISABLED -- whatever the desktop's own `EvalFuncOnEdit` says, because
that switch is the desktop user's choice for their own keyboard, not
for everyone holding a link.  Both ops find the binding through one
`boundPathOf()`.

The client: `complete.ts` -- the dot rule, local filtering, splicing,
the keyboard rule -- is pure and gated in node; `field.tsx` is the fx
button and the dialog (editor, completion list, live result, OK /
Discard), rendered by the CARD and portalled to the body.

Gates: `FormWidgets` 23 passed / 0 failed, `test_completion` covering
completion, set, clear, both preview severities, the suppressed
mid-typing case and the view-only refusal; the node gate 79 checks ALL
GREEN (62 of W1's, plus 17 for the completion rules); and
`panel-drive.js` against a serving FreeCAD in a desktop and a phone
viewport.

**What the live runs showed.**  `SketchPad.` answers 57 items (`Pad.`
answers 93), and typing on to `SketchPad.Con` narrows to `Constraints`
and `FullyConstrained` **without asking again** -- one round trip per
dotted segment, which is the whole point of the dot trigger.  The
result line reads `Property 'Con' not found in 'SketchPad.Con'` with OK
disabled, and is blank for a bare trailing dot.  On the phone the chips
are 44px, anchored to the visual viewport, and the keyboard is the text
one.

**Six defects, each found by running it rather than by reading it**:
the live host answered an EMPTY list for Pad's Length, because a
mirrored panel binds the widget and not the model (the gate's synthetic
field bound the model, so it passed); a field in expression mode still
asked for a decimal keyboard, which has no letters, so `SketchPad.`
could not have been typed on a handset at all; the result line put a
red parse error under every keystroke, where the desktop's own dialog
blanks exactly the "unexpected end of input" case; the dialog rendered
in place was trapped inside the card, because `.fc-panel` carries a
`backdrop-filter` and a filtered ancestor is the containing block for
`position: fixed`; portalling it to the body then put it BELOW the
chrome host (`#fc-ui`, z-index 10), so the HUD card covered the editor
-- it still took focus, still looked right, and every keystroke went to
whatever was on top; and the new gate case adopted store objects
without releasing them, which `Store::reset()` keeps on purpose, so
three panel-mirror cases failed three tests later.  `panel-drive.js`
now reports what covers the field it types into, so the invisible one
cannot recur silently.

**Cost** (TypeScript unless noted): the host A1 ~180 C++; the client
A2 400-550; A3 and CSS 150-250; the gates 150-200.  Total about 1k,
against 7.22's 2.2-3.3k for the panel itself.

### 7.24 What a handset found **[found and fixed 2026-09-17]**

7.22 and 7.23 were proven headless, in an emulated phone viewport, and
against a live desktop.  Then the page was opened on an actual handset,
over a Cloudflare quick tunnel, and four things were wrong at once --
three of them defects that no gate here could have caught, and the
fourth a ruling 7.23 had already made and the console had never had
applied to it.

**The chrome host takes the pointer, and two panels never took it
back.**  `#fc-ui` is `pointer-events: none` so the canvas keeps every
gesture the chrome does not claim, and each panel opts back in for
itself: `.fc-inspector`, `.fc-panel`, `.fc-hud`, `.fc-menu` and
`.fc-launch` all carry `pointer-events: auto`.  **`.fc-console` and
`.fc-sheet` never did.**  So the Python console and the spreadsheet were
click-through in their ENTIRETY -- every tap landed on the model behind
them, the close button did nothing, and both panels looked perfect while
it happened.  Two lines fixed it.

**Why no gate saw it, and why one still cannot.**  The panel harnesses
(`sheetharness.ts` and the console's) mount into a plain
`document.body` host with no `pointer-events: none` ancestor, so the
defect *cannot exist* in a harness: the thing that breaks it is the
production host, which the harness deliberately does not have.  The
check that does catch it is a real tap -- `elementFromPoint` over the
panel returns the canvas (an element with no class at all, which is how
it reads in a report), and a touch listener on the canvas counts the
event that should never have arrived.

**The launcher drew over the bottom sheet.**  Both launchers hid only
for `cardOpen()` -- the inspector card -- and not for the task panel,
the sheet or the console, and they render after the card with no
z-index, so DOM order won the hit test.  The predicate now covers all
four bottom sheets, which is the behaviour the inspector card already
had.

**A press on a narrow header never stopped.**  `draggable()` returned
early on narrow BEFORE the `stopPropagation()` whose whole job that is,
and the viewer binds `mousemove`/`mouseup` to the DOCUMENT
(`Renderer/wasm/main.cpp`), not to the canvas -- so an event that
bubbles is one it may read as an orbit.  Stated honestly: **this one was
never reproduced here.**  In an emulated phone viewport the header
absorbs both taps and drags (`canvasMove: 0` against a bare-canvas
control of 3), and Chrome's synthetic touch emits no compatibility mouse
events at all, which is precisely the mechanism a real handset would
exercise.  The change is defensible from the code rather than from a
reproduction, and the handset reported the symptom gone.

**The console completed like readline.**  It spliced in the longest
common prefix and printed the candidates into the output, which on a
phone meant: no list unless the line already ended in a dot (the one
case where more than one candidate survives the "more than one" test),
no list at all on a partial word, nothing narrowing as typing went on,
and nothing coming up on its own.  It now runs the 7.23 controller
unchanged -- `triggerFor`, `stillApplies`, `filterSet`, `splice` -- over
a guest adapter, `setFromGuest()`, which fills in the `end` the guest
does not answer (its rlcompleter works on the source up to the caret,
so the caret IS the end).  The candidates are the chip strip above the
keyboard on a narrow viewport and a list in the panel's own flow on a
wide one: `.fc-console` is `overflow: hidden`, so a dropdown over the
input row would be clipped by the panel that owns it.

**A shortcut is not an affordance.**  The console's only trigger was
Tab, and a handset keyboard has no Tab key -- so completion was
unreachable on the device that needs it most, while working perfectly
on a desktop, which is why every check here passed.  7.23 had already
ruled this for the expression field ("an explicit ask always works: a
chevron in the field's row, a tap target, which a phone needs and a
keyboard shortcut is not"); the ruling simply had never been carried to
the console.  **The general form, worth keeping: a feature reachable
only by a key that a phone's keyboard does not have is not a feature on
a phone, and no desktop gate will ever say so.**

**What proved it** (live, through the tunnel, both viewports): the
console and sheet absorb their taps and all three close buttons close;
the launcher is absent while a sheet is up; `str.` raises 20 chips with
nothing pressed, typing `jo` narrows to one WITHOUT asking again, and
the chip splices `str.join(`; a partial word with no dot (`st`) lists
two, both on its own and from the button.  The node gate carries six
new cases for the adapter, and is ALL GREEN.

**Cost**: about 120 lines of TypeScript and CSS, plus the six gate
cases.

### 7.25 Completion, paused **[handoff 2026-09-17]**

7.24 rebuilt the console's completion as a list and proved it against a
live serve.  The handset then found it still wrong, for a reason now
diagnosed but NOT yet written, and the work was paused here.  This
section is the handoff: what is known, what is designed, what is open.

**The console's real defect: rlcompleter matches a case-sensitive
PREFIX.**  `global_matches` tests `word[:n] == text`, `attr_matches`
tests `word[:n] == attr`, and our own PropertiesList loop adds
`name.startswith(attr)`.  So `app.`, `fre` and `doc.` answer NOTHING
while `App.` works -- and an empty answer renders as no list, which
from a phone is indistinguishable from "the trigger never fired".  That
one cause can produce the whole report, including it seeming to work
only from the button: on an already-dotted line the prefix is empty and
therefore always matches.

**RULED by the user 2026-09-17: completion searches for any keyword and
ignores case.**  Designed, not written:

- Enumerate the candidates and match case-insensitively ANYWHERE in the
  name, ordered prefixes first then alphabetical -- the ranking
  `filterSet` already uses, so the guest and the panel agree.
- The decoration has to be reproduced by hand, because `splice` inserts
  the item verbatim and that is why `im` becomes `import ` and not
  `import`: `_callable_postfix` appends `(`, and `)` as well when
  `inspect.signature(val).parameters` is empty; a keyword gets a
  trailing space except `{False, None, True, break, continue, pass,
  else, _}`; `try` and `finally` get `:`.  `get_class_members` walks
  `__bases__`.
- Keep rlcompleter's shelf rule: hide names starting with `_` unless the
  typed word starts with one.  With contains-matching, `init` would
  otherwise surface `__init__`.
- `console.py` already imports `builtins`; it needs `keyword`.

**The spreadsheet has no completion at all** -- and needs no new
matching rule.  `ExpressionCompleter` already defaults to
`Qt::MatchContains` + `Qt::CaseInsensitive` (`ExpressionCompleter.cpp`
2140-2145), flipped only by the `CompleterMatchExact` /
`CompleterCaseSensitive` preferences.  What is missing is the op:

- `sheet.complete`, registered NON-mutating beside `sheet.list/get/set`
  in `installSheetControlOps` -- a view-only client may complete and
  still may not write, which is 7.23's rule for `widgets.complete`.
- `resolveSheet(req, boundDoc, error)` already turns `{doc, obj}` into a
  `Sheet*`, and `ExpressionCompleter` is `GuiExport` taking a
  `const App::DocumentObject*`, so the body is
  `ExpressionCompleter completer(sheet);` then
  `completer.completionsFor(text, pos, start, end, &tips)` -- the same
  shape `widgets.complete` has at `SceneWidgets.cpp:427`.
- Client: a `sheetComplete` wrapper beside `sheetGet`/`sheetSet` in
  `control.ts`, over `sendOp`.
- The panel has TWO editors sharing `editText`/`commit`: the formula bar
  (`.fc-sheet-input`) and the in-cell editor (`.fc-sheet-cellinput`).
  Wiring one only would repeat the "works here, not there" trap this
  round hit twice.  **OPEN, asked and unanswered**: both, or the formula
  bar first.  The in-cell editor commits on BLUR, so a completion tap
  there must not blur it -- taking the pointer on `pointerdown` with
  `preventDefault`, as the chips already do, is what prevents that.
- Unlike all of 7.24 this half is C++: it needs a Gui rebuild and a
  serve restart, not `npm run build` and a reload.

**The one open question, and the cheapest way to settle it.**  Whether
the console has a SECOND defect behind the case sensitivity -- whether
an auto dot trigger fires on a real handset at all.  The test that
separates them: type `FreeCAD.` in the correct case and see whether the
list appears with NOTHING pressed.  If it does, case sensitivity was the
whole story.  If it does not, the trigger is not firing on a soft
keyboard, and the next suspect is composition: a phone keyboard fires
`input` with `isComposing` true while a word is being composed, which
this controller does not consider.

**The tools, and where they went.**  The probes that found and proved
7.24 are not committed (offered, not taken up), and a scratchpad is
session-scoped, so they were copied to `~/works/sw/fcad-probes/handset/`:

    chrome-probe.js     taps every chrome surface and reports whether
                        the tap reached the <canvas> and whether each
                        close button closes -- the one that caught the
                        pointer-events defect
    console-complete.js the dot trigger, local narrowing, the picked
                        line, and a partial word with and without the
                        button, in a phone and a desktop viewport
    console-tap.js      the completion tap target and its 44px size
    drag-probe.js       the header-drag leak, with the bare-canvas
    leak-probe2.js      positive control that proved the FIRST version
                        of that experiment inert

The puppeteer knobs they need are in `docs/Testing.md`: `PUPPETEER_PATH`,
`CHROME`, `CHROME_LIBS`, and emsdk's node.

**Getting a phone onto it again** (this session's recipe):
`FC_SERVE_TOKEN=<secret> FC_SERVE_TRUST_PROXY=1
scripts/renderer-serve.sh scripts/demo-taskpanel.py 8077`, then
`cloudflared tunnel --protocol http2 --url http://127.0.0.1:8077`.
**QUIC is blocked outbound on this box**, so without `--protocol http2`
the tunnel comes up degraded and says so.  The page is
`/fcviewer.html?doc=<name>&token=<secret>`, with `&panel`, `&console` or
`&sheet` to open a card on load.  The bundle is `npm run build` in
`src/Gui/Renderer/web` into `build/wasm/web`, `npm run gate` is the pure
gate and `npm run typecheck` the types.  The viewer bundle answers
before the door, so the PAGE loads without a token while every route
carrying scene data is gated -- do not read a 200 on `fcviewer.html` as
an open server.

**Where the code stands**: three commits, none pushed -- `ef831bc5d3`
(the chrome's pointer), `cac3af4313` (the console's list), `8f9af09904`
(7.24).  Nothing of the ruling above is written yet.

## 8. Measurements

All on this box (6 cores, `conda-relwithdebinfo-801`); the bench gtests
(`ExpressionImageBenchTest`) are `DISABLED_*` and run by hand, so
these numbers are not CI-checked and can drift.

### 8.1 Evaluation cost

    what                                  native   WASI (Release)   pyodide (final)
    ------------------------------------  -------  ---------------  ---------------
    transport floor                       --       2.68 us          5.4 us
    wire floor (eval "1")                 --       12.8-15.7 us     14.1 us (was 22.2, 30.6-37.6)
    parse + eval arithmetic               1.8 us   13.36 us (6.6x)  15.9 us (was 18.6, 23.6)
    one property read                     3.7 us   20.92 us (7.1x)  17.4 us (was 29.7, 37.1)
    one bridge hop (marginal, read_prop)  --       5.4 us (was 7.1-7.6)  4.5 us (was 4.4, 5.9-7.4)
    pack per eval (export+proxy+release)  --       +19 us (was +23) +7.4 us (was +23, +44, +27)
    instantiate + first eval              --       14 ms (.cwasm)   ~1.5-1.7 s

The pyodide "was" figures are the bytearray/getBuffer transport; the
current ones are the short-circuit of 2026-09-04 (sec 4.1: wasm
imports bound to V8 natives, exports called from C++), which took the
JavaScript out of both directions.  It cut the round trip by about 9
us and the per-eval pack cost by 17 us; the marginal hop itself moved
within noise (~7 us either way), because what remains of a hop is the
guest's CPython `__getattr__` + `_fcx.op` + CBOR (~3 us) and the host
dispatch (GIL, handle table, property read, security, encode, ~3-4
us), not the crossing.  The fixed-layout codec (step 7, sec 3.2) then
took its predicted 1-2 us: the marginal read_prop hop is 5.4 us on
WASI and 4.4 us on pyodide (2026-09-04, same bench).  What is left is
CPython on both ends; the transport is done.
    native Shape.Volume / BoundBox.ZMin   179/183 us (mass properties, not
                                          materialization: a TopoShapePy
                                          attribute read is ~1 us native)

The hop row was re-taken on 2026-09-04.  Every earlier figure for it
(WASI +31.6, pyodide +136.8 / +72.1 / +78-80 us) came from a bench that
reused ONE bindings pack across iterations: the guest copies the pack
into per-eval globals and releases the handle when those die, the host
flushes that release on the next call, so every iteration after the
first timed a stale-handle ERROR round trip.  The bench now builds a
fresh pack per iteration, asserts success inside the loop, and reports
the marginal hop as a slope over 1 and 4 reads of `o.Width` (the proxy
does not cache: `__getattr__` crosses every time).  The pack row is
what a raw `eval` with one object binding adds over the wire floor and
includes the release hop.
Pyodide's stages for the first two rows: first transport 40.5 / 89.0 us;
buffered 8.4 / 39.0 us; after `-fvisibility=hidden -flto` 5.4 / 30.6
us, "about 2x the WASI image".  The verdict from the WASI numbers:
architecture A is not a prerequisite; a 10k-cell arithmetic sheet
recompute projects to 0.25-0.31 s from 0.13 s today.  Every number
taken before the Release fix (2026-08-31) was 3 to 6x too slow; the
browser 140 us figure is one of those and was never re-taken.

The shape program (7.17 D3, 2026-09-14, `DISABLED_BenchFlangeProgram`):
the flange of 7.17 evaluated whole, 40 iterations after a warm-up --
**22.0 ms routed** under enforcement against **19.6 ms native** with
enforcement off, 1.12x, +2.3 ms, over 15 bridge ops per evaluation.  The
OCCT booleans are most of both numbers; what routing adds is the guest's
parse and evaluation, the pack and the handle traffic of fifteen
`geom.call`s, all together about an eighth of the solid.  This is sec 11
item 4's "bench of one shape program routed against native", and the
program side asks nothing more of it.

**Re-measured 2026-09-16** (sec 11 item 4's decision run, pyodide, the
same `ExpressionImageBenchTest` benches): the pyodide column above.  The
pack is **+7.4 us**, not the +23 this table carried -- `image.eval.pack.
noHop` 20.5 us less `image.eval.noPack` 13.1 us -- and the wire floor is
14.1 us.  The C5 marshal work of 2026-09-15 (7.20: interned slot names,
the facade-class cache, the last-string reuse) is where that went.  The
marginal hop is unchanged at 4.5 us.

`scripts/expr-phase0/native_bench.py`, run twice with `Evaluate` written
EXPLICITLY both ways: a 10k-cell arithmetic sheet recomputes at 15.2
us/cell native and 38.4 us/cell routed, so a routed cell carries about
23 us of fixed cost -- wire floor 14.1 plus pack 7.4, the two terms of
this table, of which the WIRE is now the larger.  Its first ten rows are
NOT a routing measurement: `DocumentObjectPy::evalExpression` calls
`Expression::getPyValue()` and never crosses the seam
(`PropertySheet::evalPy` is the seam), so they read the same in both
modes -- which is how the first run of this pair was misread, the sec 12
trap biting its own author.  `DISABLED_BenchFlangeProgram`, the decider
sec 11 item 4 names: a shape program is **1.084x** native when routed
(21.4 ms against 19.8 ms, +1.7 ms, 15 bridge ops per evaluation).

**A finding that is NOT the sandbox's, DIAGNOSED AND FIXED 2026-09-16**
(`f21f46b9f1`).  A sheet of unit-valued cells that reference anything
recomputed in **O(n^2)**.  Measured on `=Box.Height * i`, one document
per case, routing off:

    n=250     692.2 us/cell  ->  11.3        n=1000   2818.0  ->  11.6
    n=500    1394.3          ->  10.9        n=2000   5681.3  ->  11.9

Flat in n afterwards; 477x at 2000 cells.  The mechanism: a
QUANTITY-valued cell's write goes through `Cell::setComputedUnit`,
whose `AtomicPropertyChange` invokes `PropertySheet::hasSetValue()`,
and that walks EVERY cell of the sheet (`getDepObjects` on each
expression, plus the element-reference visitor) before calling
`updateDeps`.  Per cell that is O(n).  The fix is the class's own
batching: one `AtomicPropertyChange` held across `Sheet::execute`'s
recompute loop makes the inner ones no-ops (`tryInvoke` fires only at
`signalCounter == 1`), leaving one rebuild at the end -- which is what
`hasSetValue` does anyway, rebuilding wholesale from `data`.
`markChange` is false, so a pass that writes nothing still costs
nothing.  Correctness: an external change still reaches the cells
(`Box.Height` 10 -> 20 propagates) and the sheet still lists `Box` in
its `OutList`; `TestSpreadsheet` 52 OK, ctest 643/643.  This is host
code that looks upstream, not fork-specific.

**Three things this section got wrong on the way, kept because they
cost hours.**  (1) The SHAPE read was blamed and is innocent:
`=Box.Shape.Volume / i` is flat at ~158 us/cell at every size, which is
just OCCT's mass properties.  The original cell was pathological
because it ends `+ Box.Height`, making the RESULT a quantity -- a plain
number result never calls `setComputedUnit` and was never affected,
which is why a 10k-cell arithmetic sheet was always 15 us/cell.  (2)
The **56.8 ms/cell** figure carried a confound: that bench's sheet
shares a document with a 10k-cell sheet and a third sheet, and
`doc.recompute()` covers all of them; in a fresh document the same cell
is 6.5 ms/cell.  Bench sheets belong in their own documents.  (3)
Routing was never involved -- 56.9 ms/cell routed against 56.8 native.
The `getDep(true)` -> `access(..., &deps)` observation below stands as
true (collecting a dependency DOES evaluate the path, so a dependency
walk over shape cells runs OCCT), and it is why this sheet was the
worst case, but it was not the cause: `getDep` ignores its own
`needProps` (`(void)needProps;`, 2022-05-09) with the cheap early
return commented out beneath it, and the non-evaluating twin
`getDepStructural` OVER-APPROXIMATES on purpose, so neither is a
drop-in.  Both are still open, and both are the user's call.

### 8.2 The corpus gate

`scripts/expr-switchover/{corpus_regression.py, run_gate.sh,
summarize_gate.py, packaging_check.py}`: one `FreeCADCmd` subprocess
per file with a hard timeout (a single 7.7.2-to-8.0.1 forced-recompute
file can stall a whole-corpus sweep for ten minutes), enforcement OFF
in the rig (parity, not policy), paths passed through the environment
(real corpus paths contain quotes and backslashes; `FreeCADCmd` exits 0
when the script raised, so a missing summary file is the failure
signal).  Final state on WASI (2026-08-31): 372 files, 335 expressions
compared, 0 differ, 2 both-error with matching text, 2 timed out;
`scanner.FCStd`'s 73 expressions all match.  On pyodide (2026-09-02):
350 files, 334 compared, 332 same, 0 differ, 2 both-error, 1 timed
out -- the same result set.  Two parity gaps the gate could not see
(literals crossing at 15 digits, error text) were found and closed on
2026-09-02 (`f062e51e80`).

**Re-run 2026-09-14** (7.17 D3, pyodide), after the rig was found never to
have compared a spreadsheet cell -- it asked `obj.cells.getUsedCells()`,
which `PropertySheet` does not have, and the `except` made every sheet
contribute nothing, from the rig's first commit -- and to compare a shape by
a repr carrying its address.  Every total above therefore counts expression
BINDINGS only.  Fixed, the gate: 95 files under `~/works` (the corpus has
moved since 8.3), 94 compared, **195 expressions, 195 same**, 0 differ, no
error on either side, 1 timed out (`issue474_fillet_edit_crash.FCStd`, an
OCCT regression model, at 180 s).  The D3 fixtures are in the list --
`src/Mod/Test/TestData/SandboxProgram` lies under the root, while the build
and install copies are skipped -- and their seven expressions (five `Shape`
programs, the linked consumer's, the sheet's method cell) are all the same.

**Re-run 2026-09-16** (sec 11 item 3's decision run, pyodide): 95 files
listed, 94 compared, **195 expressions, 195 same**, 0 differ, no error on
either side, the same single timeout -- the 2026-09-14 result to the
number.  What this gate does NOT answer, and item 3 turns on: the rig
sets `Evaluate` BEFORE it opens its files, so all 94 do open routed, but
a Proxy the guest cannot serve fails closed silently and the rig counts
expression bindings only.  Probed separately, with no runtime installed:
routing on de-Proxied 70 of 70 objects of
`data/examples/draft_test_objects.FCStd`, routing off none.  That is the
gap the `available()` guard of 3.5 closes.

### 8.3 The corpus

456 documents under `~/works`, 7628 expressions, 362 unique strings
(2026-08-30): member reads 302, engine-builtin calls 57, foreign-document
references 69, pure arithmetic 34, one pseudo-property, one dotted
module call, zero `.Proxy` drill-down, zero `import_py`, zero `eval`.
Function frequency: `hiddenref` 29, `dbind` 17, `str` 10, `tuple` 7,
`trunc` 6, `tan` 4, `atan` 2, `sum` 2, `vector`, `ceil`, one
`_math.degrees`, one `Vector.getAngle`.  The King IFC model contributes
1742 x 4 (its `.Shape.BoundBox` bindings, 5808 cells, are host-side
TopoShape materialization the sandbox does not add to -- a handle
protocol could answer BoundBox from a host cache without building a
TopoShapePy at all); the CNC project contributes nearly all foreign
references; scanner.FCStd holds the only sub-shape drill-down.
Non-ASCII object names occur in real files.  Rig:
`scripts/expr-phase0/`.

### 8.4 The mirrors **[measured 2026-09-11, 7.19 M4]**

Xvfb, `SandboxMirrorBench` (sec 9) named alone in
`SANDBOX_GUI_GATE_MODULES`, two panel subscribers (7 writes, 8
watches): `bytes` is what client 8 got, `toWrtr` what 7 got, both
byte-exact from the pushed JSON; the counters are the mirror's own
(`FormWidgets.panelStats`), the delta over the phase; `readUs` is the
meta-object reads and the compare, `writeUs` the store writes with
the fan-out, `walkUs` the walks.  "repaint" is every content widget's
`update()` with nothing changed; "typing" ten client writes 40 ms
apart into one field (Pad: `q_rawValue` of the length; OrthoArray:
`q_value` of the X count); the tool bar rows are 7.18's mirror with
one watching client and no counters.

    scenario   phase         s    msgs  bytes     B/s toWrtr flush wRead kRead kWrit readUs writeUs walkUs models
    pad        open       0.503    64   45942   91292  45942     1    20   359     2    286      30   3188     61
    pad        rest 2s    2.106     0       0       0      0     0     0     0     0      0       0      0     61
    pad        repaint    0.099     0       0       0      0     1    17   303     0    302       0      0     61
    pad        repaint x5 0.443     0       0       0      0     5    85  1515     0   2146       0      0     61
    pad        typing x10 0.540    20    1530    2833    760    10    30   570    10   1632     345      0     61
    pad        close      0.151     5     450    2977    178     0     0     0     0      0       0      0      0
    orthoarray open       0.534    48   35145   65760  35145     2    26   452     0    362       0   2508     46
    orthoarray rest 2s    2.108     0       0       0      0     0     0     0     0      0       0      0     46
    orthoarray repaint    0.100     0       0       0      0     1    10   170     0    376       0      0     46
    orthoarray repaint x5 0.430     0       0       0      0     5    50   850     0    902       0      0     46
    orthoarray typing x10 0.540    10     712    1318      0    10    10   200     0    579       0      0     46
    orthoarray close      0.216     5     447    2069    178     0     0     0     0      0       0      0      0
    sketcher   open 48    0.586    31   29739   50785  29739     1    11   188     0    189       0   4119     29
    sketcher   rest 2s    2.091     0       0       0      0     0     0     0     0      0       0      0     29
    sketcher   repaint    0.100     0       0       0      0     1     9   152     0    333       0      0     29
    sketcher   check off  0.122     1     107     873      0     2    10   168     0    299       0    286     29
    sketcher   check on   0.129     1     107     827      0     2    10   168     0    255       0    317     29
    sketcher   move point 0.130   162   21086  162069  21086     2    10   168     0    292       0    294     29
    sketcher   close      0.151     5     450    2987    178     0     0     0     0      0       0      0      0
    toolbars   open       0.121   202  178352 1474161      0
    toolbars   rest 2s    2.110     0       0       0      0
    toolbars   select x5  2.180   110   10015    4594      0
    toolbars   to Part    0.603    67   51045   84655      0
    toolbars   to Draft   0.611    15    9878   16162      0

By message: a panel's open is `open`s (one per model) and one or two
`update`s (the list's layout, a key the post-open flush found
changed); typing is `update`s only; the close is three `custom`s
(the panel's own follow-up), one `close` and the list's `update`;
Sketcher's move is `item:clear` 2, `item:insert` 32, `item:set` 128;
the tool bar switch to Part is 56 `open`s, 10 `close`s, one order
`update`.  What the numbers decide is under 7.19 "M4 measured".  The
one hot spot is Sketcher's refill on a solve (162 ops, 21 KB per
solve, three ops a row): the case for coalescing per-row ops into one
`items` reset.  Everything else is under 1 ms of host time per burst
and nothing on the wire unless a value changed.

## 9. Tests

    file                                          cases   covers
    --------------------------------------------  -----   ----------------------------------
    tests/src/App/ExpressionSecurity.cpp            11    catalog, hash, grant store
    tests/src/App/ExpressionSecurityRuntime.cpp      9    resolve, scopes, pending, audit
    tests/src/App/ExpressionImageHost.cpp           81    programs 5 (7.17 D1: function
                                                          objects routed = native, the
                                                          flange, the surface stamp and
                                                          record), acceptance 6, bench 6 (disabled),
                                                          bridge 8, budget 4, eval 25 (the
                                                          G1a-G1d gates among them, the
                                                          Draft and BIM corpus gates, the
                                                          durable-handle gate),
                                                          host 9, routing 9
    tests/src/App/ExpressionPyodide.cpp             10    layout, verify, scoping, offer
    tests/src/App/TypeImport.cpp                    10    the type-string import rule
                                                          (sec 13): roots, dotted names,
                                                          a finder's home, stdlib,
                                                          loaded, missing
    tests/src/App/ProxyImport.cpp                    7    the Proxy import rule (sec 11
                                                          item 1) on both containers, the
                                                          pickle header
    src/Mod/Test/SandboxProxyImport.py               2    a view provider's Proxy from
                                                          outside every root refused on
                                                          reopen, one under the user's
                                                          Mod restored; the GUI gate
                                                          script, no guest needed
    src/Mod/Test/SandboxPyodide.py                   2    the offer end to end
    src/Mod/Test/SandboxGui.py                       2    G2a: commands and a workbench
                                                          registered from the guest (7.9);
                                                          needs the GUI -- run through
                                                          scripts/sandbox-gui-gate.py on Xvfb
    src/Mod/Test/SandboxWidgets.py                   2    Probe B: an ipywidgets form from
                                                          the guest rendered in Qt, driven
                                                          both ways (7.3); the same gate
                                                          script (its default module list)
    src/Mod/Test/SandboxForms.py                     3    G3a/H0: a Draft .ui form and
                                                          Draft's own OrthoArray panel from
                                                          the guest, Qt-shaped, both ways,
                                                          through the C++ store and the
                                                          C++ Qt view (7.11, 7.12); the
                                                          same gate script
    src/Mod/Test/SandboxNative.py                    1    H1b: Pad's real panel on the
                                                          host widget layer, the expression
                                                          seam (7.12); the same gate script
    src/Mod/Test/SandboxPanels.py                    2    G3b: the 40 .ui files behind
                                                          Draft's and BIM's 41 loadUi sites
                                                          opened from the guest and
                                                          round-tripped, exec_() (7.11);
                                                          the same gate script
    src/Mod/Test/SandboxSelection.py                 4    G3d: FreeCADGui.Selection from
                                                          the guest, a SelectionObject by
                                                          value, an observer as a guest
                                                          proxy, the four stock dialogs
                                                          driven from a host timer (7.11);
                                                          the same gate script
    src/Mod/Test/SandboxSessionDoc.py                9    S1, S2: the session document (7.13):
                                                          a guest command's ActiveDocument,
                                                          writes, a kept handle, two
                                                          documents, the document-principal
                                                          regression, save/saveAs, Draft_Heal
                                                          and BIM_Trash/EmptyTrash from the
                                                          guest; Gui.doCommand / addModule in
                                                          the guest (the audit and macro lines,
                                                          a document refused), Draft_Upgrade's
                                                          commit and Arch_Site through
                                                          doCommand; the same gate script
    src/Mod/Test/SandboxDraftGui.py                  3    G3c: Draft's DraftGui.py in the
                                                          guest -- the tray tool bar, the
                                                          panel built in code, a point
                                                          typed on the host, the key
                                                          stream, a menu, the main window
                                                          shim (7.11); the same gate script
    tests/src/Gui/FormWidgets.cpp                   20    H0-H1, G3b, G3c: the host widget
                                                          layer's property core, class set,
                                                          layout ops, code-built layouts,
                                                          bars, actions, the key relay,
                                                          the fwuic generator's
                                                          output for OrthoArray, the Qt
                                                          backend binding uic's widgets, the
                                                          ports, the item views, dialogs and
                                                          containers (7.12, 7.11); the
                                                          store's fan-out and the widget
                                                          stream (7.18); the panel mirror
                                                          over a real TaskBox (7.19);
                                                          FormWidgets_Tests_run, offscreen
    src/Mod/Test/SandboxToolBarMirror.py             5    7.18: the tool bar mirror under
                                                          Draft (structure, a group's
                                                          default, coalescing, the stream
                                                          with an injected sender, a
                                                          workbench switch); the GUI gate
                                                          script, no guest needed
    src/Mod/Test/SandboxModelDump.py                 6    7.18 (c): the widget model dump
                                                          run on the tree; the Python suite
    src/Mod/Test/SandboxPanelMirror.py               6    7.19 M1: Pad's C++ panel, Draft's
                                                          OrthoArray, a CAM op mirrored,
                                                          written, closed through the root;
                                                          M2: Sketcher's constraint list
                                                          reflected and checked from a
                                                          client, a QSvgWidget as a picture;
                                                          M3: a slot's QMessageBox as a
                                                          dialog root, its exec code;
                                                          the GUI gate script, no guest
    src/Mod/Test/SandboxMirrorBench.py               4    7.19 M4, sec 8.4: the panel
                                                          mirror's cost per repaint burst
                                                          and per keystroke, Sketcher's
                                                          refill, the tool bars as the
                                                          baseline; NOT in the gate's
                                                          default list (a measurement),
                                                          named alone to run
    src/Mod/Spreadsheet/TestSpreadsheet*.py          --   run with routing ON for parity

The acceptance harness opens a real saved-and-reopened `.FCStd` under a
real `document:sha256` principal and runs hostile expressions through
every layer.  Suites green at `fd14ba2878`: C++ 536/536, Python 2630;
the sandbox suites at the durable-handles commit (2026-09-05): pyodide
91/91, wasi 80 + 11 skipped, the four corpus gates passing with two
recomputes each.
Every gtest and the corpus gate select a runtime per process through
`FCX_RUNTIME`.

## 10. Configuration reference

Preferences under `User parameter:BaseApp/Preferences/Expression/`:

    Sandbox:Runtime          "pyodide" | "wasi" (default: pyodide when built)
    Sandbox:Evaluate         route evaluation through the image (default ON
                             since 2026-09-16, OFF before);
                             also routes a document object's saved Proxy
                             to the guest at open, failing closed (3.5);
                             BOTH halves also require a runtime that
                             boots, so the preference alone never
                             de-Proxies a document (2026-09-16)
    Sandbox:InitGuiInGuest   run the InitGui.py of a module whose GUI side
                             is a bundled wheel (fcx_draft, fcx_bim) in the
                             guest (default OFF; 7.9 G2b); the rig's
                             FCX_INITGUI_IN_GUEST=1|0 overrides it
    Sandbox:BudgetMs         5000        Sandbox:GraceMs   1000
    Sandbox:MemoryMB         1024: the ceiling on a guest's linear memory
                             (the Python heap) and on its array buffers,
                             from the next growth; 0 = the engine's own
                             4 GB (7.17 D4; a guest boots at 49 MB)
    Sandbox:EngineHeapMB     512: V8's heap limit for the guest, from the
                             next boot; 0 = V8's default (a guest boots
                             at 35-38 MB)
    Sandbox:ImagePath        Sandbox:StdlibPath          (WASI)
    Sandbox:PyodideDir       Sandbox:PyodideWheel        Sandbox:PyodideUserDir
    Sandbox:PyodidePackages  Sandbox:PyodideUnpinned
    Security:Enforce         default true

Under `User parameter:BaseApp/Preferences/Fw/`:

    PanelMirror              "all" (default) | "none" | a comma list of
                             TaskDialog class names: which task dialogs
                             the panel mirror streams (7.19)

Environment: `FCX_RUNTIME`, `FCX_IMAGE`, `FCX_STDLIB`, `FCX_PYODIDE`,
`FCX_PYODIDE_WHEEL`, `FCX_PYODIDE_USER`, `FCX_PYODIDE_PACKAGES`,
`FCX_PYODIDE_UNPINNED`; rig-only `FCX_GATE_ONLY`, `FCX_SHEET_REPORT`,
`FCX_REPO`, `FCX_PROBE_*`, `FCX_INITGUI_IN_GUEST`.

CMake (`cMake/FreeCAD_Helpers/InitializeFreeCADBuildOptions.cmake`
:151-177): `BUILD_EXPR_PYODIDE_HOST` (default `v8-embed_FOUND`),
`BUILD_EXPR_WASI_RUNTIME` (OFF, never turns itself on),
`BUILD_EXPR_IMAGE_HOST` (ON iff either runtime; ON with none is a
configure error), `FREECAD_PYODIDE_DIR` (bundle a distribution in a dev
tree), `FREECAD_FCX_IMAGE_WHEEL` (ship the wheel under
`<datadir>/Pyodide/wheels/`), `FREECAD_PIVY_WHEEL` (the pivy wheel of
7.10, shipped and mirrored the same way), `FREECAD_BUNDLE_WASMTIME`.  With
`BUILD_EXPR_PYODIDE_HOST` and `BUILD_DRAFT`, target `DraftSandboxWheel`
packs `fcx_draft-<ver>-py3-none-any.whl` into the same wheels
directory (5.6); `WidgetsSandboxWheel` packs `fcx_widgets` (the comm
shim, 7.3) unconditionally, and `FREECAD_BUNDLED_WHEELS` (a `;`-list of
pure wheels, `scripts/sandbox-fetch-wheels.py` fetches ipywidgets and
traitlets) is mirrored one target per wheel (`<dist>_wheel`).

Command line: `--grant <permission>[:<target>]`, `--policy <file>`.

## 11. Roadmap, one list

**Re-aimed 2026-09-08 (1.2).**  In order; each step ships alone.

**Ruled 2026-09-10, the build order of the two sizings:** the panel
mirror FIRST (7.19, M1 to M3, item 5 -- the ThinClient session told
on 2026-09-10 and asked for its panel requirements, the DOM view to
go in parallel against the panels the store already streams), and
only when it is done the document program (7.17, D1 to D4, item 2).

**Ruled 2026-09-10, ahead of item 2:** first merge `origin/RemoteEdit`
(the ThinClient shared-session branch, cut from `LinkVibe` at
`48378dec12`; docs/ThinClient.md 8.11/8.12 there) INTO `SecurePython`
-- merge, never rebase; a dry merge on 2026-09-10 conflicts in
MainWindow.cpp/.h, MainWindowPy.cpp/.h, SceneControl.cpp/.h,
ViewArea.cpp, MeshLevelSource.cpp, the web style.css and the two docs
-- then build what that session asked of the widget layer: a
multi-subscriber signal on `Gui::Fw::Store` (the fan-out point for N
streamed clients, beside the single sink), a mirror of the native
tool bars (the ToolBarManager's live QAction set) into `QAction` /
`QToolBar` models with a change notification on `setState`, and a
JSON dump of the model classes, properties and defaults generated
from the guest `models.py` for their DOM backend.  Sizing 7.17 waits
behind it.  **Merged 2026-09-10** (the merge commit on SecurePython,
338 commits from RemoteEdit at its tip): thirteen files conflicted;
the one semantic conflict was the status bar registry, which BOTH
branches had built independently (7.15 here, LinkVibe's `c324d79dfd`
there) -- resolved in favour of LinkVibe's (`StatusBarItemSpec`, the
`MainWindow/StatusBar` group, the id as the widget's objectName,
orders 0-1000 with the workbench band 550-699 between the input hints
at 100 and the notifications at 800), keeping from ours what callers
need: `MainWindow::statusBarItem(id)` and `isStatusBarItem(widget)`
(the tool bar manager's skip), the Python `statusBarItem`, and the two
sandbox indicators registered at 900 and 910 right; `SandboxGui.cpp`'s
`gui.mainwindow` ops and the `SandboxInitGui` gate follow the new
group and names.  The bgfx, OndselSolver, cycles and vg-renderer
submodules moved with the merge.  The three pieces are SIZED as 7.18
(with the ThinClient session's ten requirements of 2026-09-10 answered
there); 7.17 stays reserved for the document program.  **All three
BUILT 2026-09-10** (7.18's built note; the peer session told, the
push the user's call).

0. **The cut** (1.6, audited): RULED 2026-09-09 "freeze everything"
   -- not executed, not scheduled; the guest-GUI code stays frozen in
   place under the sec 7 header's retire-on-break rule, and 1.7
   records what the workbench path would still need.
1. **The Proxy import restriction, native.**  **BUILT 2026-09-09.**
   `PropertyPythonObject::Restore` imports only a module already in
   `sys.modules` or one `importlib.util.find_spec` resolves inside a
   registered module root (`Type::addModuleRoot`, the rule
   `Type::importModule` has had since 2026-09-05), for BOTH containers
   -- `App::DocumentObject` and `Gui::ViewProvider`; anything else is
   refused before importing and the object is left without a Proxy,
   logged.  The legacy pickle-name scan feeds the same check.  Closes
   sec 13's last file-chosen host import.  Built as sized, with one
   addition the sizing missed: a Proxy's module is DOTTED
   (`draftobjects.wire`), and `find_spec` on a dotted name imports the
   parent, so `Type::moduleAllowed` now walks the name one level at a
   time -- a level is looked up only once the levels above it are
   loaded or under a root, and a loaded stdlib package (`xml`) does not
   admit an unloaded submodule of its own.  And a second, found by the
   FEM suite's old test documents: a saved LEGACY name no file answers
   for (`femsolver.elmer.solver`) is served by a meta-path finder --
   FEM's `femtools.migrate_app` maps it onto today's module -- with a
   spec that has no location at all, which the rule read as "not under
   a root".  A spec without a location is now judged by the FINDER'S
   home: the file of the module its loader's class is defined in, under
   a root or not (a finder installed by an addon's `Init.py` from
   `Mod` passes; the same class imported from outside is refused;
   "built-in" and "frozen" are origins, so they still match no root).
   Gate: `ProxyImport.*` in Tests_run (7 cases: under a root, dotted
   under a root, outside every root, stdlib, loaded, missing, the
   pickle header; both containers), four cases added to `TypeImport.*`
   (dotted under a root, dotted under a loaded stdlib parent, a finder
   under a root, the same finder outside), and `SandboxProxyImport`
   in the GUI gate (a saved document whose view provider names a module
   outside every root reopens without that Proxy and without the
   import; one under the user's `Mod` restores).
2. **The document program** -- **SIZED 2026-09-10 as 7.17, RE-SIZED
   2026-09-13 against the proxy chain** (a `Spreadsheet::Sheet` in a
   feature's `ProxyExp` is a second carrier and runs the flange
   natively today at the speed of a host-Python `Proxy`; D1 becomes
   the precondition of BOTH carriers; P3 of docs/ProxyChain.md folds
   into D2, whose first item was the recompute fix of
   ProxyChain.md 4.5 -- BUILT 2026-09-13, ProxyChain.md 4.6): a
   statement program bound to a `Part::Feature`'s `Shape` already
   recomputes routed (the probe's bracket and stair); the one guest
   gap was function objects (`ExpressionPy` into the image, D1 BUILT
   2026-09-13: the flange routed = native to the BRep byte); the
   carrier is `App::ExpressionLibrary` on `App::TextDocument`, served
   into the guest through the import miss, namespaced per principal,
   with a dependency edge from `import` and the source in the
   principal hash; a library in another file is reached by an XLink,
   live or pinned with a snapshot (RULED 2026-09-10 against a
   same-document rule); per-document guests are OUT on the
   measurement (2.5 s, 335 MB, resets retain ~100 MB -- glibc's arenas,
   measured in D4); stages D1-D4, 1.7-2.5k lines, all four BUILT by
   2026-09-14 (D4: the memory ceiling).  As asked: (a) the example set: two or three document-carried programs (a
   parametric bracket, a stair) written in the engine's language,
   generating shapes, run routed and native; (b) the geometry surface
   audit against them -- the constructive set (`make*`, extrude,
   revolve, the booleans, fillet and chamfer, placement, the shape
   queries) added by annotation where missing, and the surface
   VERSIONED (1.5, FeatureScript): a file records the facade version
   it was written against; (c) the carrier: a document-level library
   of named functions, defined once, callable from any expression in
   that document, evaluated in the document's guest (1.5, `LAMBDA`);
   (d) per-document guests, decided by the 1.5 s boot measurement;
   (e) the gate: such a file opens with routing ON and recomputes
   byte-identical to native, and the corpus gate stays green.
3. **Routing ON by default** (`Expression/Sandbox:Evaluate`) when the
   corpus gate says so, as ruled 2026-08-31 -- not judgement.
   **BUILT 2026-09-16.**  The gate said so: 95 files listed, 94
   compared, 195 expressions, 195 same, 0 differ, no error on either
   side, one timeout (8.2) -- the 2026-09-14 result to the number.
   Found on the way and fixed BEFORE the flip: the same preference also
   routes a saved Proxy's restore, which the gate does not measure, and
   on a build with no runtime installed that de-Proxied 70 of 70
   objects of a real Draft document, so `proxyRestoreRouted()` now asks
   `available()` too (3.5; gate
   `ExpressionImageEvalTest.proxyRestoreNeedsRuntime`).  The flip
   itself is the three defaults in `ExpressionEvaluator.cpp` and the
   save/restore in `SandboxProxyImport.py`, which would otherwise read
   False from an unset key and write it back.  Measured on the flipped
   build over `data/examples/draft_test_objects.FCStd`: the key unset
   routes (70 stand-ins, 0 native), a stored `false` still wins (70
   native, so nobody who opted out is overridden), and with no runtime
   the guard keeps all 70.  Live construction is NOT affected --
   `MyClass(obj)` in a macro still builds a host Proxy, since
   `constructGuestProxy` answers only for a construction the guest
   serves.  `scripts/sandbox-reopen-routed.py` reproduces the 7.6
   reference to the number: 70 stand-ins, 0 host, 0 unserved, 0
   invalid, one shape not byte-identical (Polygon, the documented 1 ULP
   of the guest's libm `cos`).  The rest of the work was the tests,
   where an absent key used to mean off -- sec 12.
4. **Expression performance, two bounded items** (1.3, rung 1's
   replacement): the bindings pack cached per cell across evaluations
   and invalidated on document change (the 23 us per eval of 8.1);
   typed reads for the handful of property types whose `getPyObject`
   materializes a wrapper (`Shape.BoundBox`: the King IFC model's 5808
   cells, 8.3).  Measured by the existing bench and the 10k-cell sheet
   projection.  A bench of one shape program routed against native
   decides whether anything more is needed.
   **MEASURED 2026-09-16, and the measurement argues against building
   either (8.1).**  The pack is 7.4 us, not 23 -- the C5 marshal work
   took the rest -- so caching it per cell buys at most that, against a
   TWO-SIDED protocol change (the host keeping handles past
   `clearHandles()`, the guest suppressing the releases it queues as it
   unwinds) and a staleness hazard.  Typed reads buy about 1 us of a
   205 us `Shape.BoundBox`: 8.1's own note is that the 179/183 us is
   OCCT computing mass properties, NOT materialization.  And the
   decider this item names answers itself -- the flange program is
   1.084x native routed.  What a routed sheet cell actually pays is the
   WIRE floor (14.1 of its 23 us), which is neither of these items.
   OPEN FOR THE USER: whether to spend anything here at all, and
   whether the 56.8 ms/cell host finding of 8.1 is in scope.
5. **The browser's task panel** -- RULED 2026-09-10 ("B is good"):
   the PANEL MIRROR of 7.19 -- the desktop's real task panel walked
   into models and streamed, no edit to any workbench, C++ panels
   included -- with G7, the DOM view over the widget layer (7.12,
   the ThinClient session's) -- **G7 SIZED 2026-09-16 as 7.22**:
   W1-W5, 2.2-3.3k of TypeScript, one 20-line host change, the
   gate a replay of frames dumped by the panel gate; the five
   questions RULED 2026-09-16 and W1 started.  The `freecad.widgets`
   shim's op call
   bound natively (route A: Draft's panels on the host without
   pyodide, the toolkit gates' native-mode twin) drops behind it and
   stays the answer for the tier with no Qt.  H2 and H3 (7.12) are
   optional for the browser from here, kept for the Qt-free desktop
   backend.  Not a sandbox item; listed because the code is shared.
6. **An authoring panel for the document library** (item 2's carrier),
   on the widget layer -- after 5.
7. **The browser console** -- **SIZED 2026-09-11 as 7.20, RULED after
   item 2**: pyodide in the client's page, the wire over the
   WebSocket through a JSPI-suspending import (a Worker + Atomics
   path later, for Safari), a per-connection endpoint on the host
   pushing the scope per op, a `client:<identity>` principal with its
   own catalog column (v2), the panel on `pyodide.console`; stages
   C1-C6, one to two weeks.  Un-drops the 7.14 chokepoints (F1): a
   remote client is the second guest that needs them, unless `gui`
   is a hard DENY for clients -- which C3 chose.  C1 BUILT 2026-09-14,
   C2, C3 and C4 BUILT 2026-09-15 (C4: the panel, on the guest's own
   entry rather than `pyodide.console`); C5 BUILT 2026-09-15: measured (a
   loop was one op per element, too slow through a tunnel), then a
   per-statement prefetch of sibling reads, ruled the same day -- 50
   objects at 100 ms RTT, 5.4 s -> 0.42 s; the guest is +185 MB PSS in
   the page.  C6 (Safari) only if necessary (ruled 2026-09-15).

DROPPED 2026-09-08: G4 (7.16, sized), F1 (7.14: it closed a hole only
a SESSION guest has), N1-N5 (network is a session need), G5, G6, rung
1 and the App-core severance (1.3), the `Python/Runtime = pyodide`
switch as a goal.  FROZEN: G2, G3, S1, S2, the InitGui runner (sec 7
header).  The numbered list below is the HISTORY of what was built,
kept as the record; its forward items are superseded by the seven above.

The history.  Done: Phase 0 audit (2026-08-30),
Phase 1 image and router (2026-08-31), the pyodide runtime and budget
(2026-09-02), N0 demotion, P1 bootstrap and offer, G0 linter
(2026-09-03).

1. **G1** -- Draft's App side in the guest, four stages (7.6): G1a
   the surface, G1b the wheel and loader, G1c rung 2 for one principal
   (one Draft Wire `execute()` byte-identical from a guest Proxy) DONE
   2026-09-04, its two decisions ruled and the Restore route (f) built
   the same day; G1d OPENED the same day -- the Draft test document
   reopens routed with every Proxy a stand-in and every object
   recomputing (harness `scripts/sandbox-reopen-routed.py`, gate
   `draftTestObjectsReopenRouted`); the Polygon ULP question answered
   (libm, gated in ULPs), `Proxy.<attr>` host reads and the
   construction dispatch BUILT the same day (gate
   `draftTestObjectsBuiltRouted`: the whole Draft test document built
   with routing on, 70/70 Proxies in the guest); BIM remains (7.6).
2. **G2** -- U1 + U2 + U7: Draft and BIM register from the guest; the
   subset shim.  SIZED 2026-09-05 (7.9): G2a the registration mechanism
   (the guest's `FreeCADGui`, the `gui.*` op family, stand-in commands,
   wrapped workbenches, gate `SandboxGui`), G2b the InitGui runner and
   the measurement of how far Draft's and BIM's `InitGui.py` get -- their
   `Initialize()` imports forms, the snapper and trackers before one
   toolbar, so the residue is G3's list.
3. **Probe A** -- PASSED 2026-09-05 (7.10): the Coin fork compiles with
   emcc under `COIN_BUILD_GL_STUB`, `pivy.coin` loads in the guest, a
   10 k-node graph builds in 0.77 s, boot grows by 0.2 s; real Coin it
   is.  **Probe B** -- PASSED 2026-09-05 (7.3): ipywidgets and
   traitlets bundled unchanged, the `comm` shim and an `IPython`
   stand-in (`fcx_widgets`), the host manager and twenty Qt views, a
   six-widget form shown as a task panel from a guest script; 0.13 s
   import, 0.5 ms per model, 0.22 ms per slider event, +0.02 s boot;
   gate `SandboxWidgets`.  **G3a** BUILT 2026-09-06 (7.11): the Qt
   subset as models, `loadUi` on both sides, the task panel, layouts
   from `.ui` files, `prefs.write`; Draft's OrthoArray panel runs
   unmodified in the guest; gate `SandboxForms`.  **H0** BUILT
   2026-09-06 (7.12: the host widget layer -- `src/Gui/Fw/`, the same
   22 classes in C++ over a property bag, the store of the guest's
   objects, the C++ Qt view, the `PanelDialog`, `fwuic.py`,
   `FreeCADGui.FormWidgets`; OrthoArray from the guest renders through
   it and the Python Qt-shaped views are gone).  **H1a** BUILT
   2026-09-06 (TaskOrientation onto the generated form, gate
   `test_taskOrientationPort`).  **H1b** BUILT 2026-09-06 (Pad/Pocket's
   form onto the generated models, the expression seam
   `Fw::ExpressionBound` on the `QuantitySpinBox`/`DoubleSpinBox`
   models; gate `test_expressionSeam` 13/13 and `SandboxNative`, in the
   default gate list since the `ViewAreaCell` teardown fix of
   2026-09-06).  **G3b** BUILT 2026-09-06 on the C++ store (7.11: the
   item views as rows with item ops, dialogs with `exec_()`, the
   containers, the file chooser; gate `SandboxPanels`, the 40 files).
   **G3c** BUILT 2026-09-06 (7.12: forms built in code -- the layout
   tree as `layoutSpec`, bars, actions, the main window shim, the key
   event stream; Draft's `DraftGui.py` unmodified in the guest, gate
   `SandboxDraftGui`).  **G2b BUILT** 2026-09-06 (7.9: the runner in
   `FreeCADGuiInit.py` under `InitGuiInGuest`, the re-run after a
   reset, the stand-ins stamped with their guest; Draft's and BIM's
   `InitGui.py` in the guest at startup, Draft's tool bars from the
   guest; gate `SandboxInitGui`).  **GuiUp flipped** the same day
   (the guest's is the host's; gate `SandboxCorpusGui`, the G1 corpus
   under the GUI): both `Initialize()`s complete, both workbenches'
   tool bars come from the guest; `Activated()` stops at the status
   bar and dock widgets, the 3D view (G4) and the selection (G3d)
   are next.
4. **P2** -- in-place install into a running guest (the sec 9.3 probe of
   `SandboxNetwork.md`: does a wheel with compiled extensions import
   synchronously without `loadPackage`?).  Moved after G1: nothing
   built depends on it and a fresh guest boots in about 1.5 s.
5. **N1** -- `NetPolicy`, `NetClient`, `net.http.request`, the
   `XMLHttpRequest` shim, the user-level allow/deny preferences, the
   audit line.  Test: `requests.get` against a local server from a
   session guest, denied from a document guest, redirect hops.
6. **PyPI sources** for `install_package`, written once against N1's
   client.
7. **G3** -- U3 over the widget protocol, sized in 7.11 as four stages:
   G3a BUILT 2026-09-06 (the Qt classes as models, `loadUi` both sides,
   the task panel, `.ui` layouts, `prefs.write`, `Gui.ActiveDocument`);
   G3b BUILT 2026-09-06 (dialogs with `exec_()`, the tree/list/table
   family as rows with item ops, containers, the file chooser; the
   40-file harness `SandboxPanels` is its gate); G3c BUILT 2026-09-06
   (forms built in code: layouts without a file, actions, tool bars,
   menus, the main window shim, the key event stream; DraftGui's tray
   and panel are the gate, `SandboxDraftGui`); G3d BUILT 2026-09-07
   (the selection input over the host's own Selection, observers as
   guest proxies, the four stock dialogs one op each; gate
   `SandboxSelection`).  **S1 BUILT 2026-09-07** (7.13): the session
   document -- a workbench (session or addon) reaches every open
   document, reach computed from the principal (`reachable` in
   `ExpressionImageBridge.cpp`); `FreeCAD.ActiveDocument` /
   `Gui.ActiveDocument` are the host's live one; `listDocuments` /
   `getDocument` under `app.query`, `newDocument` / `closeDocument` /
   `setActiveDocument` under the new `app.write`; `save` declared,
   `saveAs` on a picker-blessed path.  **S2 BUILT 2026-09-07** (7.13):
   `Gui.doCommand` / `doCommandGui` / `addModule` in the guest under
   the `gui.doCommand` enum value, one host op for the macro and audit
   lines; Draft's commit through `todo.doTasks` and BIM's `Arch_Site`
   run end to end from the guest; gate `SandboxSessionDoc` (9 cases).
   **The status bar / dock widgets BUILT 2026-09-07** (7.15: the
   native status bar registry on `MainWindow` -- the fork had no
   `addStatusBarItem`, no `Gui::ToolBar`, and its tool bar manager
   adopted any status bar tool bar, so Draft's and BIM's status widgets
   were broken natively too -- `FreeCADGui.Command` in the guest
   binding a command's real action, `QDockWidget` through the dock
   manager, host timers for delayed callbacks, the workbench
   manipulator; both `Activated()`s run to their end; gate
   `SandboxInitGui` 7/7).  NEXT (ruled 2026-09-07): G4 -- SIZED
   2026-09-07 (7.16), its scope put to the user -- then F1.
   **F1 -- the file and code chokepoints** (7.14, SIZED 2026-09-07, not
   built): `fs.read` / `fs.write` / `host.exec` checked inside the
   core's file and `runFile` primitives under the guest's scope, the
   host's own file dialog blessing the paths it returns; closes the one
   host-execution path a guest has (`Gui.runCommand("Std_RecentMacros")`
   under `gui`).  Order RULED 2026-09-07: the status bar / dock
   widgets, then G4, then F1 (before N2: N3's network rows share the
   mechanism).
   H0 (BUILT 2026-09-06) and H1 (7.12) come before G3b so G3b's views
   are written once, in C++; H2 and H3, the native ports, interleave
   with G3b-G3d as the class set grows.
8. **G4** -- the mirror, SIZED 2026-09-07 (7.16, not built): the
   walk of real guest `SoNode`s as one typed op per guest turn, the
   host reader with its allowlist and quotas, the fork's node types as
   real SoTypes in the guest, the host roots as guest proxies, the view
   op family with the camera snapshot, the events one hop each, the
   active window and the MDI observer (G4a: `Draft_Line` from the
   guest end to end); view providers in the guest (G4b: the restore
   route, the hook table, `addDisplayMode`); the residue (G4c).  Gate:
   `SandboxScene` -- the host's and the guest's `SoWriteAction` text of
   a mirrored subtree equal, the drawing loop under synthetic mouse
   events -- and the corpus reopened with the view providers routed;
   the pixel compare is a secondary check.  Its scope is put to the
   user before building.
9. **N2** -- `fetch` and the loop primitive.  **N3** -- network rows in
   the existing permissions panel, the addon manifest at install time.
   **N4** -- WebSocket (`net.ws:<origin>`), and `SOCKFS` under its own
   `net.socket` permission.
10. **G5** -- the snapper in C++, when measured to matter.  **G7** --
    the DOM walker over the widget layer in the browser tier (7.12,
    SIZED 2026-09-16 as 7.22; no Draft/BIM gate of its own): every
    panel, guest or native, in the browser from the same models.
    The Qt-free manager over bgfx (RmlUi or imgui) is DROPPED, 7.4.
11. **Rung 1** -- generated C++ dispatch for `call` members; the
    App-core severance.  **Rung 2** -- per-document guests.
12. **N5 / G6 / the switch** -- `Python/Runtime = pyodide`, Draft and BIM
    with an empty GUI island, the full Draft and BIM suites green with
    routing ON, the browser drawing a Draft line end to end.

Not on the roadmap: a socket-level host capability, UDP, listening
sockets, any network for the reference image, a webview escape hatch.

## 12. Traps

- **A panel inside `#fc-ui` that never sets `pointer-events: auto` is
  invisible to the finger** (found on a handset 2026-09-17, 7.24).  The
  chrome host is `pointer-events: none` so the canvas keeps everything
  the chrome does not claim, and each panel opts back in for itself --
  miss it and every tap goes through to the model, the close button
  does nothing, and the panel still looks exactly right.  Nothing
  warns.  No gate here can catch it either: the panel harnesses mount
  into a plain `document.body` host with no such ancestor, so the
  defect cannot exist in a harness.  What catches it is a real tap --
  `document.elementFromPoint` over the panel answers the canvas (an
  element with no class), and a touch listener on the canvas counts an
  event that should never have reached it.
- **moc drops the rest of the class after a raw string holding an
  unbalanced `(`** (found 2026-09-17, 7.23's gate).  A case that feeds
  the host an expression which deliberately does not parse wants
  `R"("text":"2 * ("})"` -- and moc's lexer ends the literal at the
  first `)"` it believes it has found, loses the class, and emits a
  meta object with NO slots at all.  Nothing warns.  The build fails
  much later, at link, with `undefined reference to vtable for
  testFormWidgets`, which points at the class rather than at the
  string, and the stale test binary keeps passing in the meantime.
  Proving it takes one command -- run `.conda/freecad/lib/qt6/moc`
  over the file and count the `test_` names in its output: 0 with that
  literal, 21 without.  Write such a case as an escaped ordinary
  string.  The file is full of legitimate `R"({...})"` JSON, which is
  fine: it is the unbalanced parenthesis, not the raw string.
- **The tool bar manager owns every `QToolBar` under the status bar**
  (7.15).  A bar a workbench puts in the status bar itself is adopted
  into the manager's `StatusBarArea`, hidden on the next workbench
  switch, saved in its state and pre-created empty and untitled at the
  next start -- so a name lookup finds a bar that is not the one you
  made.  Register through `MainWindow::addStatusBarItem` (the manager
  skips registered items) and, when looking a host bar up by name,
  accept only one with a title.
- **A model's constructor parent does not cross.**  Only `setParent`
  on an open comm does; a host-side rule that needs the parent (the
  `widgetForAction` bind) must have the guest send it, and must run on
  custom events too, not only on open and update.
- **A repeating guest timer stopping itself** stops from inside the
  host timer's own timeout: `deleteLater`, never `delete`.

- The GUI process hung in `exit()` once a 3D view refined a mesh
  (found 2026-09-06 under gdb, the corpus gate; FIXED 2026-09-07):
  `pthread_cond_destroy` on the static condition variable of
  `src/Mod/Part/Gui/MeshLevelSource.cpp` with its refine threads still
  waiting on it -- glibc's destroy blocks until every waiter has left,
  and a detached worker parked on a static never does.  The fix keeps
  the workers joinable and stops and joins them in
  `PartGui::shutdownMeshLevelWorkers()`, hooked to `aboutToQuit` and
  to a Qt post routine the first time a worker starts; the mutex and
  condition variable are leaked on purpose so an exit that never ran
  the hook cannot hang either.  A worker inside BRepMesh has no safe
  interruption point, so a build in flight finishes before the join.
  The rig no longer `os._exit`s.  `gdb -p` is refused on this box
  (ptrace scope): run the binary under gdb and send the inferior
  SIGINT.
- `Gui::ActionGroup::actions()` cached raw `QAction*` past deletion
  (fixed 2026-09-06): a group's action may belong to another command,
  and replacing that command left a dead pointer `Command::testActive`
  read on the next UI tick.
- A `FREECAD_USER_HOME` pointing at a nonexistent directory is silently
  ignored and writes the REAL `user.cfg`; an ad-hoc gate run once left
  `Enforce=0` there.
- **An absent `Evaluate` key stopped meaning "off" on 2026-09-16**, when
  routing became the default (sec 11 item 3).  Twelve cases in three
  modules broke on it.  `SandboxProgram.route(False)` removed the key
  and then asserted `routed()` was False.  The tampered-library case
  used the same idiom for its "natively and enforced" step, so that
  step ran ROUTED, where `import Part` is served by the guest instead
  of being `host.import` -- the withheld grant blocked nothing and the
  consumer stayed Up-to-date instead of going Invalid.
  `FeaturePythonChainCases` and `ViewProviderChainCases` set routing
  nowhere at all, while their hooks are recorded on the HOST and a
  Proxy restored into the guest calls nothing there (a view provider's
  own Proxy is never routed, but the EXTENSION of a view chain is a
  document object whose Proxy is).  The distinction to keep: removing
  the key INSIDE a save-and-restore is right -- it restores "unset",
  which is what was there, and every `PrefGuard` in the Test modules
  does exactly that; removing it to MEAN off is a bug.  A case that
  wants native writes `SetBool("Evaluate", False)`.
- The image directory is read-only after an install: the compiled
  module cache must live under `<user cache>`; its file name hashes the
  full image path (collision if two images share a base name).
- `libwasmtime.so` has no SONAME: `IMPORTED_NO_SONAME` is honoured only
  on a target CMake knows is SHARED.
- Two `nlohmann::json` copies collide on exported symbols carrying
  `json` in the signature.
- The v1 catalog ALLOWs addons: a refusal test needs a document
  principal.
- Pyodide's shell-mode `resolvePath` is the identity (5.4); a dev tree
  staging numpy beside the runtime makes an offer test pass vacuously.
- `ImageHost::evalExpression` must hold its `Security::Runtime::Scope`
  across the round trip, or a mid-evaluation bridge op (`pkg.missing`)
  has no principal unless an outer entry pushed one.
- The pyodide guest module must be `add_executable` with a `.so`
  suffix; `add_library(MODULE)` becomes an `emar` archive.
- Windows and macOS path handling in the pyodide scoping is by
  construction only; nothing ran there.
- `pre-commit` and `black` are not on this box's PATH.
- A guest `reset()` does not give all its memory back: RSS 462 -> 557 ->
  633 -> 659 MB over three resets (2026-09-10, 7.17), about 100 MB
  retained each, decelerating.  MEASURED TO ITS CAUSE 2026-09-14 (7.17
  D4): not a leak.  glibc's in-use heap is back to 7-8 MB after every
  reset; what stays is FREE memory in glibc's arenas -- the main arena
  and one per V8 worker thread (18 heaps, 16 of them ~26 MB each, after
  three resets) -- because glibc raises its mmap threshold each time a
  large mmapped chunk is freed, so the next boot's buffers and compile
  zones land in the arenas and stay there.  A fixed `M_MMAP_THRESHOLD`
  (256 KB or 2 MB, the same) removes it: post-trim RSS flat at 152-185
  MB over six resets, against 252 -> 488 MB over ten without.  Built:
  `malloc_trim(0)` after a guest's isolate is disposed: RSS after five
  resets 236 -> 303 -> 367 -> 407 -> 417 MB, against 316 -> 433 ->
  492 -> 465 -> 482 MB without it; the process-wide threshold is
  NOT set (it changes every allocation FreeCAD and OCCT make; RULED
  2026-09-14: leave it).  Reset for a package install only; a library edit drops one
  module (`lib.drop`), never the guest.
- V8 calls the near-heap-limit callback from the LAST-RESORT GC that
  follows a failed array buffer allocation, not only at the heap's
  limit: a callback that stops the guest there stops it for a refused
  buffer.  The runtime's tells the two apart (7.17 D4).  And returning
  the current limit from it at a real limit is V8's fatal out-of-memory,
  which ends the process.
- The guest reaches the shim's JavaScript realm (pyodide's `js`): a shim
  check that looks up `Reflect.construct`, `fn.call` or a prototype
  getter when it runs hands the native to the guest's replacement or
  reads its lie.  Take every intrinsic when the shim loads, read bytes
  through the slot getters into a copy, convert an argument once (7.17
  D4, the compile gate).
- In JavaScript, `var X = function Memory() {}` sees ITSELF as `Memory`
  inside its body: a wrapper named after the native it wraps must keep
  the native under another name.
- The WASI stdlib slice has no `importlib`: guest prelude code imports
  with `__import__` and walks dotted names by hand.
- `FeaturePythonT::Proxy` is private; tests reach it through
  `getPropertyByName("Proxy")`.  A `PropertyFloat::setValue` with the
  value already held fires no `onChanged`.
- A stand-in decoded twice must be the SAME object (the `write_prop
  Proxy` path and the `proxy_new` reply both carry the descriptor): a
  duplicate dying would drop a proxy still in use, hence the live table
  in `ExpressionGuestProxy.cpp`.
- Two pre-existing, non-security bugs noted in passing and not fixed:
  `calc()`'s non-inplace `OP_MOD` branch also calls
  `PyNumber_InPlaceRemainder`; `ObjectIdentifier::Component::del`
  falls through to an unconditional throw after a successful delete.
- The facade generator's XML list is read at CONFIGURE time (both
  guest trees and the main tree take their DEPENDS from
  `--list-xmls`).  A file that joins `ANNOTATED_XMLS` after a tree was
  configured is edited in vain: the table never regenerates, and the
  symptom is the guest reporting `'FeaturePython' object has no
  attribute 'changeAttacherType'` while the host table has it.  Since
  2026-09-04 the generator is a `CMAKE_CONFIGURE_DEPENDS` of both, so
  editing it reconfigures; before that, reconfigure by hand.
- A guest proxy's class is composed from the object's extensions when
  the handle is made.  An extension added by the guest itself
  (`addExtension` inside `__init__`) is invisible to that class until
  the `ext` op recomposes it -- which `addExtension` now does; any
  other host-side path that adds an extension mid-transaction would
  need the same.
- The guest's `math` is the runtime's libm: cos/sin can round one ULP
  from glibc (Polygon, 7.6).  Any gate that compares guest trig to
  native must compare in ULPs, not bytes.
- A value handed out by a handle is a COPY unless it is stamped: native
  FreeCAD's `obj.Placement.Base = v` works only because __getattro
  stamps the Placement as the object's attribute and `startNotify`
  writes it back.  The guest lost every such nested write until the
  proxies stamped what they hand out (3.2); a workbench that works
  natively and silently misplaces geometry in the guest is this shape
  of bug (ArchFrame, 2000 mm).
- `python-mode` through `evaluate()` is the EXPRESSION engine, not
  CPython: no `obj`, the owner is addressed by name (`Frame001.Base`),
  no comprehensions, member access through ObjectIdentifier.  It
  cannot exercise the stamping above; probe such things through a
  guest Proxy or the corpus gates.
- A module property that raises AttributeError reads as "module has
  no attribute": the guest's `FreeCAD.ActiveDocument` property maps a
  refused `get_attr` to RuntimeError so the reason shows.
- The Gui binary has NO `-t` mode: `FreeCAD -t Module` starts the GUI
  and sits there until killed (the console binary's FreeCADTest path is
  not run by `Gui::Application`).  A test that needs the GUI runs from a
  script FreeCAD executes at startup (`scripts/sandbox-gui-gate.py`),
  under Xvfb with `env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb` --
  `QT_QPA_PLATFORM=offscreen` crashes in `QOpenGLWidget` before any
  script runs.  Judge by the result file the script writes, not by the
  exit code.
- The Emscripten toolchain confines `find_library` to its sysroot: a
  cross project naming a library outside it (the wasm Coin archive)
  sets the path directly.
- The console binary does not flush `stdout` at exit: a probe script
  run as `FreeCADCmd script.py` that `print`s its facts loses them --
  write to stderr and flush.
- A host `activateWorkbench` whose Python `Initialize` raises shows a
  modal "Workbench failure" box: under Xvfb a hang until the timeout.
  Call a guest workbench's `Initialize` through `exec` when probing.
- The GUI gates run with `FREECAD_USER_HOME=/tmp/fchome`, where no
  pyodide runtime is installed: without `FCX_PYODIDE` pointing at one
  every sandbox case SKIPS ("the sandbox image did not boot") and the
  gate still says `RESULT OK`.  Read the case lines, not the verdict.
  The same under Xvfb: a document left open at exit is a save prompt,
  and a prompt is a hang until the timeout -- a probe closes its
  documents and dialogs before the main window.
- The guest prelude (`ImageMarshal.cpp`) and dispatcher
  (`ImageDispatch.cpp`) are compiled into the GUEST image: an edit
  needs `cmake --build build/pyodide-guest` (outside the conda env,
  docs/DevEnvironment.md) and the mirror step (`--target
  fcx_image_wheel` on the host), not a host rebuild -- the host build
  silently keeps the old wheel.  Since G3c the prelude delegates every
  `FreeCADGui` name it lacks to `freecad.widgets.gui.attr`, in the
  wheel the HOST build packs: a new name is a host build.
- The fork's sequencer holds a wait cursor until a poll queued on the
  event loop sees the sequence over, and the wait cursor's filter eats
  every key and mouse event: a test that recomputes and then sends
  keys or clicks without returning to the loop sees them vanish
  (SandboxNative's pad, then SandboxDraftGui's Enter).  Spin the loop
  until `QApplication.overrideCursor()` is None first.
- A `def` later in a class body wins over an earlier one of the same
  name: a stub left behind (`installEventFilter: pass`) silently
  replaced the real method above it.  The host-python harness of the
  guest package (a stubbed `_fcx`/`FreeCAD`/`FreeCADGui`/`draftutils.
  params`) shows such a thing in a second; the gate takes a minute.
- A tuple crosses the wire as a tuple (ipywidgets' `_options_labels`),
  a list as a list; compare with `list()` on the host when the guest's
  type is traitlets' choice.
- ONE recompute never meets a cached handle.  The handle table is
  cleared by every expression evaluation (`ExpressionEvaluator` ->
  `clearHandles()`), and a Spreadsheet's numeric cell IS an expression,
  so a Proxy that kept a document object on `self` (ArchReport) failed
  only on the SECOND routed recompute of the corpus.  A gate that
  recomputes once proves nothing about state kept across hooks; the
  corpus gates and the harness recompute twice (3.2, durable handles).
  And `clearHandles()` inside a nested round trip would drop the OUTER
  hook's live arguments -- it is a no-op while nested.
- A `call` op's arguments (`"a"`) cross as ONE wire-encoded list, not
  a JSON array: a host-side check that reads `req["a"][0]` as a JSON
  string sees nothing (the `saveAs` blessed-path check did, 2026-09-07:
  every path was "''").  Decode with `decodeHostValue` first, as
  `callWithWireArgs` does.
- Any command calling `doc.copyObject` aborted with "still being
  filled in" (fixed 2026-09-07, `Gui::Application::refreshLiveLoad`):
  the import reads its fragment through the restore path, which sets
  the Restoring bit, and the live-load claim took that for a load the
  user was watching -- so the copied object's view provider attach
  (a write of `<copy>.ViewObject`) tripped `checkUserEdit` inside the
  command.  Restoring WITH Importing is an import into an open
  document and is not claimed now.  Found by `Draft_Heal` run from the
  guest (7.13, S1); the native command failed the same way.
- A gate reading guest state through a DOCUMENT principal needs that
  principal's document open: after closing every document (the "None
  with nothing open" case) the owner is dead, its scope is the session,
  and the read prompts for `host.import` -- make a fresh document for
  the reads.  And a document's `Name` after `closeDocument` raises
  natively too: keep the name before closing.
- **A comma straight after a digit belongs to the NUMBER** (probed
  2026-09-13): `min(1,2)` is `1.2`, not the smaller of two arguments,
  and `vector(1,2,3)` and `[1,2,3]` are syntax errors.  A space after
  every comma is required, not style.  It bites hardest where it is
  silent -- a two-argument call that quietly became a one-argument one.
  The rule is the European decimal comma, `ExpressionParser.l:381`
  (`<INITIAL>` only, so python mode is exempt).
- **`range` is not a builtin of the engine language.**  `for ... in
  range(...)` works ROUTED, where the guest is Python and `range` is
  Python's; the same program run natively (the parity twin of 7.17
  (e), the corpus rig) must count with a `while`.  `pi` is a CONSTANT
  token, so `math.pi` is a syntax error where `math.sqrt(4)` is 2.0.
  And a module name is not in scope by itself: `Part.makeBox(...)`
  raises "Property 'Part' not found" until an `import Part` in the
  same program -- which is `host.import:Part`, PROMPT for a document,
  so a headless gate must grant it.  A grant is keyed by the
  document's CONTENT hash, so editing an expression voids it: re-grant
  after every edit, or the next evaluation fails as unpermitted.
- **A statement compiled out of an unbraced `if` leaves the `if` the
  NEXT statement** (found 2026-09-13, 7.17 D1).  `WhileStatement::
  _getPyValue` read `if(limit>0 && (++count % limit)==0)` over
  `#ifndef FC_EXPR_IMAGE` / `Base::Sequencer().checkAbort();` /
  `#endif`, then `continue;`.  The host compiled what it reads; the
  image build made the `if` govern the `continue`, the `switch` fell
  out to its `break`, and every `while` in the image ran exactly ONCE
  -- silently, returning the first iteration's state: the routed
  flange had one bolt hole of six.  `for` has no such block.  The
  2026-09-10 probe table records `while` as working routed, which holds
  only for a loop that needed one pass.  Any `#ifdef` inside a
  brace-less body is this bug.
- **A printed expression must re-parse to the SAME program, and a
  called lambda did not** (found 2026-09-13, 7.17 D1).
  `CallableExpression::_toString` printed its callee without the
  priority check, and `LambdaExpression` had the default priority, so
  `(lambda k: k * k)(i)` printed as `lambda k : k * k(i)` -- a lambda
  whose body calls `k`.  The printed form is what a document saves and
  what the router ships to the guest, so the program changed on save
  natively, and on every routed evaluation.  Fixed: a lambda's priority
  is 0, a `def` keeps 20, the callee prints with the check.  When a
  routed result differs from native, round-trip the expression through
  `toString()` natively before suspecting the guest.
- **A one-line compound statement printed without its line end did not
  parse again** (found 2026-09-13 by 7.17 P3, older than it; FIXED the
  same day).  The grammar's `suite` is `simple_stmt NEWLINE`, and the
  lexer supplies no NEWLINE at the end of a one-line text, so an
  expression that is ONE compound statement with its body on the same
  line -- `if 1: 2`, `while 0: 1`, `for i in [1]: i`, `def f(): return
  1` -- parsed from `...\n` but printed without it, and the print was a
  syntax error ("unexpected end of input, expecting NEWLINE").  A sheet
  cell `=def m(obj): obj.Marker = 10` saved to a file failed on reopen
  natively, and every routed evaluation of one failed at once, the
  router shipping the printed form.  Text that already spans lines
  parses as printed (`x = 1\nif x: 2`, a multi-line `def`).  Fixed in
  `Expression::toString`: a whole expression whose last statement needs
  a line end and whose print has no newline gets one; nested printing
  goes through the stream form and is unchanged, so no text that parsed
  before prints differently.  Gate: `ExpressionStatementPrint.
  oneLineCompoundStatementParsesAgain`.
- **A FUNCTION BODY'S IDENTIFIERS ARE NOT DEPENDENCIES, AND THE BODY
  READS THEM LIVE** (probed 2026-09-13).  `VariableExpression::
  _getIdentifiers` returns early while `_FunctionDepth` is non-zero
  (`Expression.cpp:4163`), and `LambdaExpression::_visit` raises that
  depth around the body (`:6529`), so nothing a `def` or `lambda` body
  references is a dependency of the cell holding it -- while the body
  still resolves those names against the sheet's live frame when it
  runs.  A cell `=def m(obj): return r * 2` returns 10; change `r`'s
  cell to 9; the cell is NOT re-evaluated, the function object is the
  SAME object, and it returns 18.  **Behaviour changed with nothing
  observable changing: no revision, no touched property, no new
  function object.  Nothing in the engine can see it, and nothing
  built on top of the engine can either.**  It is deliberate -- a
  body's names may be its own parameters, and registering them would
  hang spurious dependencies and cycles off the cell -- and it is
  older than any of this work, true of every spreadsheet function.
  **The idiom: a method's inputs come through the object it is handed,
  never through the sheet's frame.**  A shared constant that must live
  in a cell is bound on the consumer with an ordinary expression,
  which IS tracked.  See docs/ProxyChain.md 4.5.
- **A link to a `Spreadsheet::Sheet` never reports the sheet as
  changed** (probed 2026-09-13, 7.17's re-sizing).  `Sheet::getRevision()`
  is a literal `return 0` (`Sheet.h:91`), and a link property reports
  itself touched by comparing the target's revision
  (`PropertyLinks.cpp:789`, `:1107`), which
  `Document::_recomputeFeature`'s recompute optimization then reads
  through `Property::testStatus`.  So a plain `PropertyLink`, an
  `PropertyXLinkList` and `ProxyExp` alike are blind to a cell edit --
  by design, because a sheet's consumers are meant to be driven by the
  expression engine's cell dependencies.  A method carried in a cell
  is not, hence the recompute fix of 7.17 D2 (BUILT; the gate is
  `skipRecompute()` as well as `mustExecute()`, ProxyChain.md 4.6).
  The same blindness
  covers a function stored on a linked object from Python
  (`L.expExecute = f`), which is no Property at all.  Before the fix,
  an edited method reached its instances only through an explicit
  `touch()`.
- **`OptimizeRecompute` is a PERSISTED parameter.**  Flipping it from
  a probe (`ParamGet("User parameter:BaseApp/Preferences/Document").
  SetBool("OptimizeRecompute", False)`) writes `user.cfg` and silently
  changes every later run on the box -- which is how the first reading
  of the trap above came out wrong, every probe after the "proof"
  running with the optimization off.  Any parameter a probe sets has
  to be removed again (`RemBool`), and a result that contradicts an
  earlier one is a reason to check `user.cfg` before believing either.
- **`DocumentObject::_revision` is uninitialised** (`DocumentObject.h:853`,
  no constructor sets it -- the link properties DO initialise theirs,
  `PropertyLinks.h:701`).  Twelve fresh `App::FeaturePython` objects in
  one document read `[37, 0, 32374, 0, 32374, ...]` where an
  initialised member gives twelve zeroes.  It has never surfaced
  because the number is never used as a number: never serialized,
  never ordered, never compared against a constant, only
  `linkRevision(target) != stored` against a snapshot of that same
  object.  The one read of the initial value -- a link's first
  `isTouched()` -- fails safe, because the link's side is a defined 0
  and the compare therefore says "touched", costing one extra
  recompute.  Real undefined behaviour, no observable consequence, and
  the Python `Revision` attribute unreadable for anything.
- **An engine function resolves a free name through its CALLER's
  frames** (read 2026-09-13, 7.17 D2).  `makeFunc` captures nothing --
  the function is a copy of its body -- and `EvalFrame::getVar`'s
  `BindQuery` walks every frame on `_EvalStack` when the name is used.
  So a `def` sees whatever the code that calls it has bound: dynamic
  scoping, true of every cell and binding since the language existed.
  For a library that is wrong twice over -- its functions would not see
  the module's own names (`hole_d`, a helper defined below), and would
  see each consumer's locals instead.  A library's functions therefore
  carry the module's dict and run on an evaluation stack of their own
  whose base frame holds it (`CallableExpression::setGlobals`,
  `EvalStackSwap`, Expression.cpp); every other function keeps the old
  rule.  Gate: `SandboxProgram.testFunctionDoesNotSeeItsCaller`.
- **A module named exactly like a document object is unreachable with
  a dot** (probed 2026-09-13, 7.17 D2).  `import Shapes; Shapes.f(1)`
  in a document holding an object `Shapes` fails "Property 'f' not
  found in 'Shapes.f'": the identifier is resolved as that object when
  it is parsed, and the frame is asked only for an identifier that
  names no object.  `from Shapes import f` works.  This is why an
  `App::ExpressionLibrary` with an empty `Module` answers to its Name
  with the first letter lower-cased (`Shapes` -> `shapes`), never to
  the Name itself -- which would shadow itself every time.
- **A line holding only a comment is a syntax error outside python
  mode** (probed 2026-09-13).  `ExpressionParser.l:258` takes `# text`
  up to, but not including, the newline, so a comment after code works
  (`hole_d = 4  # sizes`) and a comment-only line leaves a bare NEWLINE
  where a statement must start: "syntax error, unexpected NEWLINE".
  Only an EMPTY `#` line has a rule of its own (`:262`); python mode
  has the full one (`:256`).  A library text therefore cannot open
  with a comment block today.  The lexer output is committed
  (`lex.ExpressionParser.c`) and regenerated by hand, so the fix is a
  flex run of its own, not a side effect of anything here.
- **An XLink made while a document had no file dangled when the linked
  document closed** (found 2026-09-13 by 7.17 D2's linked library,
  older than it; FIXED the same day).  `PropertyXLink::setValue` gives a
  cross-document link its `DocInfo` -- the thing that watches the linked
  document and detaches the link when it closes -- only when BOTH
  documents have a file, and otherwise delays it to `Save()`.  In between
  nothing watched: `closeDocument` on the linked document left `_pcLink`
  on the deleted object and the next read segfaulted
  (`PropertyXLink::getPyObject`, or `ExpressionLibrary::getHolder`
  through `Source.getValue()`).  Any `PropertyXLink`, a plain dynamic
  one included.  The fix keeps such links in a registry on `DocInfo`
  (`untrackedLinks`) until a `DocInfo` takes over or the link lets go,
  and one delete-document slot detaches them as `slotDeleteDocument`
  does a tracked one, the object name kept.  Gate:
  `DocumentTest.xlinkOfAnUnsavedDocumentDetachesWhenTheTargetCloses`,
  the linked document saved and not.
- **A consumer of a failed library waits Touched, it does not turn
  Invalid** (7.17 D2).  A library whose text does not parse, whose
  `Source` does not resolve or is no library, fails its own recompute,
  and the document skips what depends on it: the consumer keeps its old
  value and the Touched mark.  Look for the reason on the library.
- **A routed program's local read through a dot failed in a document
  named longer than 15 characters** (found 2026-09-14 by 7.17 D3's
  fixtures, older than them; FIXED the same day).  `v = vector(1, 2,
  3); v.x` in a document named `Abcdefghijklmnop` came back "Property
  'v' not found"; in `Abcdefghijklmno` it gave 1.0, and natively both
  did.  The bindings pack asks `referencesForeignDocument` whether an
  identifier it could not resolve reaches another document, and that
  read the name as `const std::string& docName =
  id.getDocumentName().getString()` -- a reference into the `String`
  that `getDocumentName()` returns BY VALUE, destroyed at the end of the
  line.  Up to 15 characters the dead bytes sit in the short-string
  buffer and read back intact; past it they are freed heap, the empty
  name reads as a foreign one, and the host's own failure to resolve the
  local is shipped for the guest to raise verbatim.  Every test document
  before D3 had a short name; `ProgramFlangeSheet` and a re-saved
  `saved-ProgramBracket` did not.  Gate:
  `ExpressionRoutingTest.localMemberInALongNamedDocumentStaysLocal`.
- **The guest's `sin` and `cos` are not the host's** (measured
  2026-09-14, 7.17 D3).  `sin(240deg)` is -0.8660254037844384 natively
  and -0.8660254037844385 routed -- one ULP -- while `cos(240deg)` and
  the degree arithmetic agree; the guest links its own libm.  So a
  routed shape program is byte-identical to native only where every
  trigonometric result happens to round the same way: D1's flange at a
  44 mm bolt circle is, the same flange at 40 or 50 mm is one ULP off in
  two vertices.  `SandboxProgram` holds the committed fixtures to the
  byte and a parameter change to 4 ULPs, the bar `SandboxCorpusGui`
  already uses.

## 13. Known gaps and open questions

- **`FreeCAD.GuiUp` in the guest: RULED 2026-09-06, the host's
  value** ("flip it"; 7.9).  What it costs until G4, measured by
  `SandboxCorpusGui`: an App-side hook calling its VIEW PROVIDER'S
  PROXY methods (BuildingPart's `ViewObject.Proxy.onChanged` -- 3
  objects invalid when routed under a GUI; Layer's
  `change_view_properties`, an error line) -- a host Python object's
  method, undeclared by design, until the view providers live in the
  guest; and a document guest importing a module whose GuiUp branch
  builds widgets (`ArchRebar` -> `bimcommands` -> `DraftTools` ->
  DraftGui's tray): refused, Rebar's Proxy stays native.  A document
  principal's widget traffic is not deferred (7.9).  Draft's
  preference observer (`_param_observer_start`) is a declared no-op:
  no change notification crosses to the guest yet, so a preference
  edited on the host does not refresh the guest's tray or grid.
- **Widgets in the status bar and dock widgets**: BUILT 2026-09-07
  (7.15).  What stays out: the IFC status widgets (no `nativeifc.
  ifc_status` in the guest), the property editor's corner buttons
  (`findChild(QTabWidget, "propertyTab")`), the host's "Model" dock for
  `tabifyDockWidget` and `ifc_viewproviders`, the Views tree's icons
  (`ViewObject.Icon` is a host handle), `FreeCADGui.addDocumentObserver`
  (BimSelect's observer stands down), `FreeCAD.isRestoring()` always
  False in the guest.
- **`Gui.runCommand` from a guest runs any host command under `gui`**
  (7.14, sized): `Std_RecentMacros`, `Std_RecentFiles` and
  `Std_DlgMacroExecuteDirect` run a file or open one with no picker,
  so a session or an addon reaches host Python and host files by name.
  A document principal cannot (`gui` DENY, not promptable).  The answer
  is F1, the chokepoints at the primitives, not a list of names.
- **A guest view provider's scene is G4's** (7.13, S2 built note;
  G4 sized 2026-09-07 in 7.16):
  `Arch.makeSite()` from the guest builds `_ViewProviderSite` in the
  guest, whose `attach`/`onChanged`/`updateData` reach `Annotation`,
  `RootNode` and `SwitchNode` -- Coin nodes, the mirror -- and print
  an AttributeError each from the host's hook call (the object and
  the document are right; the terrain switches are not built).
  `Gui.ActiveDocument.ActiveView` in the guest is the active-object
  REGISTRY only (`getActiveObject`/`setActiveObject`); every other
  view member is an AttributeError naming G4.  (`Gui.doCommand`
  itself, S2, is BUILT 2026-09-07; the session document is S1.)

- Document OPEN imports, before any expression runs.
  `PropertyPythonObject::Restore`'s `PyImport_ImportModule` on a
  document-chosen module name is CLOSED for document objects with
  routing on (G1c step (f), 2026-09-04): the module is imported in the
  guest, the instance allocated there, the property holds the stand-in,
  and a module the guest cannot serve fails closed (3.5, 7.6).  CLOSED
  2026-09-09, natively, for BOTH containers (sec 11 item 1): the native
  import in `PropertyPythonObject::Restore` -- a document object's Proxy
  with routing off, a VIEW PROVIDER's Proxy always -- and the legacy
  pickle header's module name go through `Base::Type::moduleAllowed`
  first; a refused name is logged with the container's full name and
  the object is left without a Proxy.  A consequence worth knowing: a
  Proxy class from a module in site-packages (a pip-installed addon
  that is not under any `Mod`) restores only if its module is already
  loaded when the document opens; `__main__` (a macro's classes) is
  always loaded.  CLOSED 2026-09-05, natively: `Base::Type::importModule`'s
  type-string import (a saved property type `Foo::Bar`, a
  PropertyPersistentObject) was a plain `PyImport_ImportModule` of
  whatever name stood before the `::` -- stdlib and site-packages
  included.  User ruling: "restrict it natively, only Mod directories or
  already loaded modules".  It now imports only a module already in
  `sys.modules` or one `importlib.util.find_spec` resolves into a
  registered module root (`Type::addModuleRoot`: the installation's
  `Mod`, the user's `Mod`, the macro directory's `Mod` and every
  `--module-path`, registered by `Application::initApplication` before
  the init script -- the same directories FreeCADInit puts on
  `sys.path`); anything else throws `Base::RuntimeError` without
  importing, and a module that exists nowhere fails with the
  interpreter's own error as before (the callers log it).  Gate:
  `TypeImport.*` in Tests_run (ten cases: under a root, dotted under
  a root, on `sys.path` outside every root, stdlib, a dotted name under
  a loaded stdlib parent, a meta-path finder under a root and the same
  one outside, already loaded, missing, core prefixes).
- What BIM's App side cannot do in the guest (the corpus gates' list):
  `ArchSchedule`'s IFC branch imports `nativeifc` (ifcopenshell) --
  nothing of it is in the guest.  CLOSED 2026-09-05: the four writers
  to objects other than the owner (`ArchStairs` makes and writes its
  railing objects, `ArchPipeConnector` trims the pipes it joins,
  `ArchSchedule` and `ArchReport` fill and recompute their Result
  sheet) are same-document writes under the revised `doc.write.self`
  (3.2, 7.6 decision 1); and the one parameter WRITE a recompute made
  (Arch areas, hatches, tessellation toggling TechDraw's
  `allowCrazyEdge` around a projection, ignored in the guest so a
  section of an edge over 10 m differed from native) is gone --
  `TechDraw.findShapeOutline`/`makeGeomHatch` take `allowCrazyEdge=True`
  as a keyword, a scoped `DrawUtil::CrazyEdgeAllowance` on the host,
  and the preference is never written by document code, natively
  either.  A nested write-back's refusal is silent, as it is natively:
  a guest `obj.Placement.Base = v` on a foreign object raises inside
  `startNotify`, which clears it.
- CLOSED 2026-09-05: a Proxy that keeps a document object on `self`
  across hooks (ArchReport's `self.spreadsheet`, ArchSchedule's, the
  `self.obj = obj` of several `onDocumentRestored`s) held a HANDLE of
  the transaction that minted it and the next hook found it stale.
  Durable document-object handles (3.2: a `[document, name]` key on
  every DocumentObject and Document handle, guest-side re-resolution on
  a stale id through `resolve`, `__eq__`/`__hash__` by key, re-resolution
  of a stale argument while decoding) fix it, and the corpus gates
  recompute twice.  Still transaction-scoped: a VALUE object kept across
  hooks (a shape, a curve) -- natively a copy, in the guest a
  `ReferenceError` on the next hook; no corpus object does it.
- Every GUI registration from the guest (7.9) runs as `session`: the
  addon principal for a workbench's guest code, and how a stand-in
  carries it for the hooks the host calls later, is G2b's.
- `getMainWindow()` is a shim (G3c): `addToolBar`, `mainWindowClosed`,
  `showMessage`, `cursor()` -- its document windows (`getActiveWindow`,
  94 corpus sites, `getWindows`, `setActiveWindow`) are the mirror's
  (G4); `QDockWidget` and `addStatusBarItem` are not in the subset.
- The forms (G3a, 7.11): a guest InputField's `text` is the guest's
  `Quantity.UserString` ("120 mm"), the host's its own decimals
  ("120.00 mm") until a host edit sends the host's -- the corpus parses
  the text, never compares it; a code-built widget's `sizeHint()` and
  `QFontMetrics` are estimates (G3c), `hasFocus()` follows the focus
  events the host relays, a `QTimer` fires once at the next drain and
  never repeats, `QMenu.popup()` blocks like `exec_()`, a `QPainter`
  records polygons, rectangles, ellipses and lines only (no text, no
  images); `.ui` strings are untranslated in the guest
  (`translate` is the identity there) while uic's are translated on
  the host -- a guest `setText(translate(...))` therefore shows the
  English; a widget re-added to a layout it is already in duplicates
  the item, as Qt does; `Gui.ActiveDocument` carries `resetEdit` and
  `Document` only.
- The GUI live expression editors evaluate as session, unconfined.
- The memory ceiling (7.17 D4) sees pyodide's memory grow only through
  JavaScript: a module the guest compiles through `js` that IMPORTS that
  memory can grow it with wasm's `memory.grow`, found by the post-trip
  check and bounded until then only by the time budget.  A second
  memory is shut out by the compile gate; catching the growth in flight
  would take a wasm validator, and only a guest process under an OS
  memory limit (docs/ComputeBoundaries.md) covers every path.
- Addon principal granularity (per addon, per file?) is still open.
- Whether a document network grant may ever be "always" (a
  content-hashed identity makes it safe against tampering; a
  long-lived grant is still a long-lived channel).
- The PyPI resolver: host-side against PyPI's JSON API, or micropip in
  the guest with a host-fed index.
- Whether the mirror reader is the widget manager with a second model
  family (Probe A answered real Coin, Probe B built the manager);
  widget-protocol versioning between a shipped guest wheel and an older
  host (the manager refuses a newer major; today it reads no version);
  the browser-only tier's rendering of the mirrored subset.
- The forms' wheels are BUNDLED (5.6), not in the user's package set
  as 7.3 first said: ipywidgets is not in the pyodide lock and there is
  no PyPI source yet (roadmap 6), and a form library the workbenches
  depend on should ship with the FreeCAD that renders it anyway.  Open:
  whether the `IPython` stand-in should yield to a real IPython a user
  installs later: the bundled wheels load before the package set, so
  a real IPython's files would land over the stand-in's in the guest's
  site-packages -- untested, and it would bring the 1.5 s import back.
- The audit log's retention and where the Report view shows it; the
  addon manifest format.
- The widget stream (7.18): no theme-change event yet -- a desktop
  icon-theme change is a stylesheet change, and a client's icon cache
  keyed (theme, name) is refreshed by re-subscribing; a guest form's
  own layout ops (`commCustom` with `layout`) are forwarded to the
  backend and not announced on `Store::message`, so an `all`
  subscriber sees a guest form's layout as it was at open and at
  `notifyLayout`, not its later incremental ops (the mirror's bars
  re-send their whole layout, so the tool bars are exact); the
  `command` op's `index` is behind that op's `Sketcher_Create*`
  allowlist until dialogs mirror (docs/ThinClient.md 8.11), the model
  path is the live one.
- The panel mirror (7.19, M1 to M3): a box's header icon has no
  name to send and no image key to carry it; a panel that grows and
  shrinks a list one row at a time and re-reads every item on each
  change (Sketcher's constraint list) sends one op per row and per
  changed cell, uncoalesced -- measured (8.4): 162 ops, 21 KB per
  solve on 48 rows, three ops a row, the case for one `items` reset
  instead; a row hidden by the view is found on the view's next
  repaint, not at once; the re-read cost per repaint burst is 0.3 ms
  for 10 to 20 widgets and nothing on the wire (8.4).  The watch has no
  `Resize`: the bag carries no geometry.  A `QToolBox` is a
  `QTabWidget` to a client.  A dialog root (M3) costs the process an
  application-wide event filter while a `panels` subscriber is up
  (one hash lookup per event); a picture takes the mouse but no keys
  beyond Return and Escape; the platform's native file dialog is not
  mirrored (the client's own picker); a non-native `QFileDialog`'s
  file views reflect every row the file system model shows,
  uncoalesced and unmeasured.

## 14. Sources and what the audit found

    source                        what moved here                    stale in the source
    ----------------------------  ---------------------------------  ---------------------------------
    ExpressionSandbox.md          secs 1, 2, 3 (model), 4-7 (arch),  opening line "nothing here is
                                  8 (ladder), 9 (gaps), rulings      built"; getvar "disabled" (deleted);
                                                                     generator output names; secs 10-11
                                                                     spent
    ExpressionSandboxPhase0.md    frozen contracts (2.1-2.3),        every file:line (Phase 1 rewrote
                                  corpus (8.3), measurement verdict  the cited functions); "commented
                                                                     out" module check is superseded
    ExpressionImage.md            4.2, 4.3, 4.4, 3.2, 3.4, 3.5,      last line "WASI stays the shipping
                                  8.1, 8.2, traps                    default"; two dangling references
                                                                     to a section that never existed
                                                                     (now 3.2's deferred release);
                                                                     option line numbers; test counts
                                                                     (7 -> 8, 44 -> 59)
    PyodideHost.md                4.1, 5, 8.1                        sec 9 "else wasi"; "to become
                                                                     PyodideHost.cc"; three installer
                                                                     functions and two parameters
                                                                     undocumented
    SandboxNetwork.md             6, 1.2, 11                         N3 describes the panel as unbuilt
    SandboxGui.md                 7                                  sec 1 survey counts (grep, not
                                                                     the linter); sec 8 first-pass
                                                                     numbers no longer reproduce;
                                                                     sec 10.5 rollup omitted 10 files
    SpreadsheetRemote.md sec 6    4.3 (browser preview numbers)      140 us is a WASI -O0 number

## 15. References

- Electron `remote` deprecation: https://github.com/electron/electron/issues/21408
- Qt Remote Objects: https://doc.qt.io/qt-6/qtremoteobjects-index.html
- VS Code: https://code.visualstudio.com/api/references/contribution-points,
  https://code.visualstudio.com/api/extension-guides/tree-view,
  https://code.visualstudio.com/api/extension-guides/webview
- Zed custom rendering: https://github.com/zed-industries/zed/discussions/37270;
  https://zed.dev/blog/zed-decoded-extensions
- Figma plugin security: https://www.figma.com/blog/an-update-on-plugin-security/;
  https://www.figma.com/plugin-docs/manifest/
- Jupyter widgets: https://github.com/jupyter-widgets/ipywidgets/blob/main/packages/schema/messages.md;
  https://ipywidgets.readthedocs.io/en/latest/examples/Widget%20Low%20Level.html;
  https://github.com/ipython/comm; https://github.com/jupyter-widgets/ipywidgets/issues/3209
- JupyterLite: https://jupyterlite.readthedocs.io/en/stable/howto/configure/kernels.html;
  https://github.com/jupyterlite/pyodide-kernel
- pythreejs: https://github.com/jupyter-widgets/pythreejs/blob/master/CONTRIBUTING.md;
  xwidgets: https://github.com/jupyter-xeus/xwidgets; euporie:
  https://euporie.readthedocs.io/en/latest/apps/console.html;
  qtconsole issue #382: https://github.com/jupyter/qtconsole/issues/382
- Cash App: https://code.cash.app/native-ui-and-multiplatform-compose-with-redwood;
  https://code.cash.app/zipline; https://github.com/cashapp/redwood/releases
- Office.js: https://learn.microsoft.com/en-us/office/dev/add-ins/develop/application-specific-api-model
- Fusion 360: https://help.autodesk.com/cloudhelp/ENU/Fusion-360-API/files/CommandInputs_UM.htm;
  Onshape: https://cad.onshape.com/FsDoc/uispec.html
- Server-driven UI: https://medium.com/@aubreyhaskett/server-driven-ui-what-airbnb-netflix-and-lyft-learned-building-dynamic-mobile-experiences-20e346265305;
  Unity UI Toolkit: https://docs.unity3d.com/6000.3/Documentation/Manual/ui-systems/introduction-ui-toolkit.html
- MCP Apps: https://modelcontextprotocol.io/extensions/apps/overview
- RmlUi: https://github.com/mikke89/RmlUi; Yoga: https://github.com/react/yoga;
  Dear ImGui bindings: https://github.com/ocornut/imgui/wiki/Bindings;
  Slint license: https://github.com/slint-ui/slint/blob/master/LICENSE.md
  (the in-canvas route these were surveyed for is dropped, 7.4; the
  viewer-side survey is `docs/ViewerUIResearch.md`, the browser
  chrome ruling `docs/ThinClient.md`)
- Deno permissions: https://docs.deno.com/runtime/reference/permissions/;
  Node permission model: https://nodejs.org/api/permissions.html;
  Spin outbound HTTP: https://spinframework.dev/v3/http-outbound;
  pyodide.http: https://pyodide.org/en/stable/usage/api/python-api/http.html;
  urllib3 emscripten: https://urllib3.readthedocs.io/en/stable/reference/contrib/emscripten.html;
  Emscripten networking: https://emscripten.org/docs/porting/networking.html
- pyodide releases: https://github.com/pyodide/pyodide/releases;
  v8-embed: `~/works/sw/v8-embed-feedstock`
