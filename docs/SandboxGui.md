# GUI for sandboxed Python: the FreeCAD UI protocol, not a PySide shim

Status as of **2026-09-03**: discussion draft; the G0 linter is built
(sec 8) and G1 is sized (sec 9).  **Revised the same evening, sec 10:**
pivy runs in the guest and the scene is mirrored (U5/U6), forms use
the Jupyter widget protocol with ipywidgets in the guest (U3), and the
toolkit transition away from Qt goes through the widget managers.
Secs 4-7 are kept as written for the record; where sec 10 supersedes
a paragraph it says so in place.  Written
on the user's framing of the same day: "since our final goal is to run
everything Python in pyodide, eventually we'll need to find a way to
expose GUI function ... my current thought is to have some kind of
bridge, or PySide shim, so that we don't expose PySide to user code
directly."  The ultimate test named by the user: **port Draft and BIM**
(Arch is folded into BIM in this fork) to run in the guest.

This is the concrete plan for what `docs/ExpressionSandbox.md` sec 8
called rung 4 and left as two "honest paths": a native GUI island, or
eliminating Python GUI glue over time.  The answer here is the second
path, made specific: the glue is not eliminated, it is **re-targeted
from Qt's API to FreeCAD's**, which the browser tier needs anyway.

## 1. What Draft and BIM actually use (measured 2026-09-03, this tree)

    area                     Draft            BIM
    -----------------------  ---------------  ---------------
    Python files             243              224
    files importing PySide   120              87
    files importing pivy     26               19
    .ui files                19               49

Across both, 98 distinct Qt classes are named, which sounds like "all
of Qt" until the uses are sorted:

    use                                              count   nature
    -----------------------------------------------  ------  ----------------
    QApplication.translate                           145     i18n, data
    QIcon / QPixmap / QImage                          342     icons, data
    Qt.* enums                                       245     constants
    QMessageBox / QFileDialog / QInputDialog          ~110    modal primitives
    QTimer (mostly singleShot, the ToDo queue)          64    "defer"
    QObject / QAction / QToolBar / QLabel / QPushButton
      / QLineEdit / QComboBox / QCheckBox / QSpinBox
      / QTreeWidget / QTableWidget / layouts ...      ~700    widget trees
    QApplication.setOverrideCursor / clipboard          45    host services

Task panels: 25 are loaded from `.ui` through `Gui.PySideUic.loadUi`,
about 12 are built by hand; the big hand-built widget is `DraftGui.py`
(2298 lines, the Draft toolbar with its X/Y/Z inputs).

pivy: 48 distinct `coin.So*` classes, 555 uses.  Where they are
matters more than how many:

    Draft/draftviewproviders    144 uses / 7 files   annotations (dimension, text, label ...)
    Draft/draftguitools         150 uses / 8 files   trackers + snapper
    BIM                         241 uses / 19 files  view providers and tools (axes, section plane, ...)

`gui_snapper.py` is 1799 lines and `gui_trackers.py` 1768; 40 files
install 3D-view event callbacks.  The snapper runs Python on every
mouse move.

FreeCADGui API: `Gui.addCommand` (82), `Gui.InputHint` / `HintManager`
(75, already data-shaped), `Gui.Selection.*` (120), `Gui.doCommand`
(26), `Gui.Control.showDialog/closeDialog` (24), `Gui.Snapper.*` (a
Python object hung on the Gui module, 36), `Gui.ActiveDocument.
ActiveView` (14), `setEdit/resetEdit` (12), and view-provider proxy
hooks (`attach`, `updateData`, `onChanged`, `getIcon`, `claimChildren`,
`setEdit`, `unsetEdit`, `doubleClicked`, `setupContextMenu`, display
modes, drag/drop): a few hundred implementations over ~16 method
names.

**Reading of the numbers.**  Most of the "Qt" is data: strings to
translate, icons to reference, enums, one-line modal dialogs, a timer
used as "run this later".  The genuinely Qt-shaped code is the widget
trees (forms and the Draft toolbar) and the Coin scene graphs
(annotations, trackers).  Those two are where the design work is; the
rest is a bridge op each.

## 2. The decision: a FreeCAD UI protocol, not a PySide shim

The user's instinct -- a shim, so user code never touches PySide -- is
right.  The refinement is **whose API the shim speaks**.  It should be
FreeCAD's, not Qt's, for four reasons that each stand alone:

- **Chattiness.**  A faithful Qt proxy is one synchronous bridge op
  per property read, per method call, per signal.  Electron shipped
  exactly that as its `remote` module and removed it: "slow,
  race-condition-prone, produces objects that are subtly different to
  regular JS objects, and a huge security liability", every call a
  synchronous IPC round trip (Jeremy Rose, and the deprecation issue
  #21408).  Our hop is 8.4 us (`PyodideHost.md` sec 9), cheap enough
  for a form, hopeless for a snapper reading the scene on every mouse
  move.  Qt's own answer to remoting a QObject, Qt Remote Objects,
  replicates properties and signals source-to-replica and forwards
  slots replica-to-source -- a model-sync shape, not a call-per-access
  shape, and that is the shape sec 4 adopts for forms.
- **Security.**  PySide is the process: `QApplication.instance()`,
  `QProcess`, `QFile`, the main window's children.  A "PySide minus the
  dangerous parts" shim is a denylist, and the node experiment already
  showed what denylists are worth (`ExpressionImage.md`, "Pyodide-on-
  node"): the security of the guest comes from what does not exist in
  it.  A FreeCAD UI protocol is an allowlist by construction, generated
  the way the other facades are (`ExpressionSandbox.md` sec 7.5),
  every op annotated with the permission it needs.
- **Portability.**  The browser tier has no Qt; its chrome is DOM
  (`docs/ThinClient.md`).  A Qt-shaped shim would have to be
  re-implemented over DOM widget by widget.  A FreeCAD-shaped protocol
  has two renderers -- Qt widgets on the desktop, DOM in the browser --
  and one guest-side API, so Draft's task panel is the same code in
  both.  This is the "unified semantic protocol" `docs/ComputeBoundaries.md`
  sec 6 wants for the thin client, the distribution boundary and the AI
  interface; the sandboxed Python is its fourth consumer, and the
  thin client's control channel (`ThinClient.md` sec 4.2) is its
  first draft.
- **Honesty about the residue.**  Some Draft/BIM code builds Coin
  nodes because there was no other way to draw a dimension.  A pivy
  shim would preserve that accident across the boundary at 60 Hz.  A
  protocol forces the question "what is this code trying to show?",
  and the answer (sec 4, U5) is a small set of annotation primitives
  the bgfx renderer needs to exist anyway.

What the shim IS: a compatibility module in the guest, `PySide` by
name, covering the DATA-level subset the survey found -- `translate`,
`QIcon(path)`, `Qt` enums, `QMessageBox.question/information`,
`QInputDialog.getText`, `QFileDialog.getOpenFileName`, `QTimer.
singleShot` -- each mapped to one protocol op, so that the 80 % of
Draft/BIM files that only touch those port with no edit.  Anything
past the subset is an `ImportError` naming the protocol op to use.
`pivy` is not provided at all; sec 4 U5 replaces it.

## 3. Chattiness budget (why the split of sec 4 falls where it does)

    interaction                           hops per event   verdict
    ------------------------------------  ---------------  ------------------------
    command activation                    1                trivial
    form: 30 fields read on Accept        1 (batched)      trivial
    form: one field changed               1 (event)        trivial
    selection change                      1 (event)        trivial
    updateData on a property change       1 per property   fine (memoized by rung 2)
    mouse move, snapper IN the guest      picks + N geometry queries per move   NO
    mouse move, snapper ON the host       1 coalesced event per frame           fine

The snapper is the only piece the boundary cannot carry as it is.
Everything else is one hop per USER event, which the 8.4 us transport
does not notice.

## 4. The protocol, in seven capability sets

Each set is a generated facade (annotated ops, sec 7.5 style), each op
carries the `gui` permission of `ExpressionSandbox.md` sec 3.2 --
DENY for documents, ALLOW for session and addons, which is exactly
Draft/BIM's standing (`addon:Draft`).

- **U1 Registration (data).**  `addCommand` (name, icon, menu text,
  tooltip, accelerator, `Activated`/`IsActive` as guest entry points),
  `addWorkbench` (toolbars, menus, icon), `addIconPath`,
  `addLanguagePath`/translations, preference pages (`.ui` + a
  parameter path -- already data).  One op each at startup, nothing at
  run time.  Icons cross as resource references, never as bitmaps.
- **U2 Host services (one op each).**  `message`/`question`/`input`
  dialogs; `fileDialog` (needs an `fs` grant -- the first place the
  sandbox meets the filesystem, and it is the user picking a file, the
  browser's `<input type=file>` shape); `overrideCursor`; `clipboard`
  (grant); `defer(ms, callback)` for the ToDo queue; `InputHint` /
  `HintManager` (already data); `updateGui` becomes a no-op with a
  note (the host owns the event loop).
- **U3 Forms (model-sync).**  *(Superseded in part by sec 10.2: the
  model-sync protocol is the Jupyter widget protocol and the guest
  library is ipywidgets; `.ui` stays the authoring format.)*  The form language is **Qt Designer
  `.ui` XML**, chosen because 68 of them already exist in Draft/BIM
  and because it is a declarative widget tree with object names,
  which is precisely a model.  The host loads the `.ui` (desktop: real
  widgets; browser: a DOM renderer over the same XML -- the property
  inspector of `ThinClient.md` sec 4.3 is its first form); the guest
  gets a proxy tree keyed by `objectName` with typed property
  get/set (`text`, `value`, `checked`, `currentIndex`, `enabled`,
  `visible`, items of a combo/list/tree) and signal subscription
  (`clicked`, `textChanged`, `valueChanged`, `currentIndexChanged`,
  `itemSelectionChanged`).  Reads are served from a mirrored model
  the host keeps in sync (one event per change), writes are batched
  per guest call.  Task panels: `Control.showDialog(panel)` where the
  panel names a `.ui` and implements `accept`/`reject`/
  `getStandardButtons`/`needsFullSpace` as entry points.  Docked
  persistent widgets (the Draft toolbar) are the same thing with a
  dock placement.  Hand-built widget code (`DraftGui.py`, ~12 panels)
  is rewritten as `.ui` plus logic -- the one real rewrite in the
  form layer, and a net simplification.
- **U4 Selection, view, document GUI state.**  `Selection.get/add/
  remove/clear/has` plus observers as events; `ActiveView` camera
  get/set, `getPoint`/pick queries, `setEdit`/`resetEdit`;
  `runCommand(name)` (a registered command by name, under the `gui`
  permission like every other UI action).  `doCommand(src)` is
  different in kind and gets its own permission (user decision,
  2026-09-03): it is an eval primitive whose source string is
  routinely assembled from user data -- object names, file paths --
  and it was the way Draft commands reached the interpreter so the
  macro recorder would see them.  Two rules:
    - **It never escalates.**  The source runs under the CALLER's
      principal with the caller's grants, in the caller's guest.  A
      document calling `doCommand` gets document grants, an addon
      gets addon grants; there is no path from any principal to
      "run this as the session".  The recording into the macro
      stream is a separate effect, and the stream marks the
      principal of every recorded line.
    - **It is gated by `gui.doCommand`**, added to the sec 3.2
      catalog of `ExpressionSandbox.md`: DENY for documents (not
      promptable -- a file has no legitimate reason to eval strings
      through the GUI), ALLOW for the session, PROMPT for addons with
      the "always" scope persisted per addon, the `host.import:<m>`
      shape.  Every call is one audit line carrying the principal
      and a hash of the source.
  Draft's 26 uses are addon-principal calls that pass the gate once
  per addon and then run at Draft's own grants, which is what they do
  today minus the ambient authority.
- **U5 View providers and the annotation scene.**  *(Superseded by
  sec 10.1: pivy runs in the guest and the guest's Coin graph is
  mirrored into the host scene; the primitive list below survives as
  the mirror's node-type allowlist.)*  The proxy hook set
  stays exactly as it is -- host calls guest entry points (`attach`,
  `updateData`, `onChanged`, `getIcon`, `claimChildren`, `setEdit`,
  `unsetEdit`, `doubleClicked`, `setupContextMenu`, display modes,
  drag/drop), which is already the rung 2 shape of a Proxy.  What
  changes is what `attach` may build: not Coin nodes but an
  **annotation scene** the host owns -- retained-mode primitives with
  parameters: polyline, marker set, text/label (font, size, alignment,
  screen-space or world), arrow/dimension line with extension lines
  and a value label, image, face set, transform, switch/visibility,
  pick style, color/material.  The host renders them in Coin today,
  in the bgfx renderer, and streams them to the web viewer as node
  types in the `SceneDump`.  Draft's dimension, text, label, axis,
  section-plane, working-plane and grid displays are all expressible
  in that set; the survey's 48 `So*` classes collapse into about a
  dozen primitives.  Per class the choice is "rewrite the proxy over
  primitives" or "port the view provider to C++"; the dimension is
  the likely C++ candidate (it is the most used and the most
  performance-sensitive), the rest rewrite.
- **U6 Interactive tools: the snapper and the trackers move to the
  host.**  *(Revised in sec 10.1: the trackers stay Python in the
  guest, mirrored like any other Coin graph; only the snapper and the
  event stream remain U6, and the snapper's move to C++ is deferred.)*
  `Gui.Snapper` becomes a C++ snap engine (endpoint, midpoint,
  center, intersection, perpendicular, extension, parallel, grid,
  working plane, ortho, the affinity rules) driven from the host's own
  event loop, with trackers (rubber-band line, wire, rectangle, arc,
  circle, ghost of a shape, grid, working-plane tracker) built on U5
  primitives.  The guest runs a tool session: `tool.begin(kind,
  options)` -> a stream of coalesced events (`point moved` already
  snapped, `point picked`, `key`, `escape`, `text entered` from the
  toolbar) at frame rate -> the guest's state machine (`gui_lines.py`
  and friends, which are small once the snapper is out of them) ->
  `tool.end()` with the geometry.  This is the largest item in the
  plan and the one with the biggest payoff beyond the sandbox: the
  web tier gets interactive drawing (the engine compiles into the
  viewer), and snapping stops costing a Python call per mouse move.
  The alternative -- snapper in the guest with coalesced events --
  fails sec 3's budget: each move needs picks and geometry queries
  against host objects.
- **U7 The compatibility module** of sec 2 (`PySide` subset), and a
  porting linter that lists, per file, every Qt/pivy use outside the
  subset with the protocol op it maps to.  Run over Draft/BIM it IS
  the work list.

## 5. What third-party addons see

An addon that uses PySide directly stays on the native GUI island at
`addon:<name>` trust until it ports (`ExpressionSandbox.md` sec 8.2),
exactly as today, and the switch (`SandboxNetwork.md` sec 11) shows it
as native.  The compatibility subset and the linter are the porting
kit; an addon whose Qt use is within the subset needs no change.  The
island empties addon by addon; nothing gates on it.

## 6. Roadmap, with Draft/BIM as the gate at every step

*(G3-G5 are revised in sec 10.6; G0-G2 and G6 stand.)*

Each step ships alone and is measured on Draft/BIM under the existing
rigs (`docs/Testing.md`; the BIM rig of the Draft/BIM port; the
routing-ON parity habit).

- **G0 -- the survey as a tool (DONE 2026-09-03, sec 8).**  U7's
  linter over `src/Mod/Draft` and `src/Mod/BIM`: per file, the
  Qt/pivy/Gui uses and their bucket (subset / U1..U6 / unmapped).
  Output: the work list and the unmapped residue, which is what sec 4
  may have missed.  `scripts/sandbox_gui_lint.py`.
- **G1 -- App side in the guest (rung 3/4 shaped, no GUI).**
  `draftobjects`, `draftgeoutils`, `draftfunctions`, BIM's objects'
  `execute()` run in the guest.  Needs the Part facade to grow to
  what they call (`Part.makeLine/makeCircle/...`, `Shape`/`Wire`/
  `Face` methods), generated from the XMLs as before.  Gate: recompute
  the Draft and BIM test documents with routing ON, byte-identical
  shapes to native.  This step alone is most of the "Python
  workbench" value and carries no GUI risk.
- **G2 -- U1 + U2 + U7.**  Draft and BIM register their commands and
  workbenches from the guest; the subset shim covers translate, icons,
  enums, modal dialogs, defer.  Gate: both workbenches activate and
  every command that needs no panel and no 3D interaction runs
  (Upgrade/Downgrade, Draft-to-Sketch, Array over selection, BIM
  utilities).
- **G3 -- U3 forms + U4.**  The `.ui` loader as a model-sync proxy,
  task panels, the Draft toolbar rewritten as `.ui`.  Gate: every
  Draft/BIM task panel opens from the guest and round-trips its
  fields; the BIM setup and views dialogs work.  The DOM renderer
  for the same `.ui` follows in the thin client on its own schedule.
- **G4 -- U5 annotation scene.**  Primitives in the host, streamed
  to the web viewer; Draft dimension/text/label and BIM axis/section
  plane view providers rewritten (or ported to C++ where chosen).
  Gate: the Draft test documents render identically (pixel compare
  on a converged scene, per the existing rule) with the proxies in
  the guest and no pivy in the process.
- **G5 -- U6 snapper and trackers.**  The C++ snap engine and tracker
  set; `gui_lines`, `gui_wires`, `gui_circles`, `gui_rectangles`,
  `gui_move/rotate/scale` on the tool-session API.  Gate: the
  interactive Draft tools under the GUI test rig (xvfb, scripted
  events), and a measured per-mouse-move cost with zero guest hops.
- **G6 -- the switch for Draft/BIM.**  Under `Python/Runtime =
  pyodide`, both workbenches run with an empty GUI island.  Gate: the
  full Draft and BIM Python test suites green with routing ON on the
  desktop; the browser tier drawing a Draft line end to end.

G1 and G2 have no dependency on each other; G3 and G4 depend on G2;
G5 depends on G4 (trackers are primitives); G6 on all.

## 7. Decisions (taken by the user, 2026-09-03)

1. **`.ui` as the form language**, subset-defined, rather than a new
   schema.  Argues for: 68 files exist, Designer exists, the DOM
   renderer only needs the subset of widget classes the survey found.
   Against: `.ui` is Qt-shaped in places (layouts, size policies) the
   DOM renderer will approximate.  DECIDED: `.ui`.
2. **The snapper goes to C++** (host and viewer), rather than staying
   Python with a coalesced event stream; sec 3 is the reason, and the
   web tier gets it.  DECIDED: C++.
3. **Annotation view providers: primitives by default, C++ for the
   dimension**, the rest per class as G4 finds them.  DECIDED.
4. **`doCommand` is permission-controlled**, not merely re-targeted:
   runs under the caller's principal, never escalates, gated by
   `gui.doCommand` (DENY document / ALLOW session / PROMPT addon),
   audited -- U4 above.  DECIDED, on the user's amendment of the
   draft's "run in the session guest and record".  Any addon relying
   on it to reach native Python is by definition on the island.
5. **G1 before G2**, G2 in parallel if hands allow.  DECIDED.

Decisions 6-8 (2026-09-03, evening) are in sec 10; decision 3 is
withdrawn there and decision 1 amended.

Next: G1 (sec 9 says what it needs) and the two probes of sec 10.6.

## 8. G0 results: the linter and what it found (2026-09-03)

*(The numbers here are the first pass, against secs 4-7 as written.
Sec 10.5 re-runs the linter with its table remapped to the revised
decisions; the tool's `RULES` now carry that remap.)*

`scripts/sandbox_gui_lint.py` is the U7 porting linter.  Pure standard
library, Python >= 3.10 (Draft and BIM use `match`; the box's system
Python is 3.8, so run it as `.conda/freecad/bin/python3
scripts/sandbox_gui_lint.py`).  It parses every `.py` under the roots
with `ast`, resolves the local names bound to `FreeCADGui`, `PySide*`
and `pivy` (imports, `from` imports, `import ... as`, lazy imports
inside functions, `Gui = FreeCADGui` assignments), and records every
attribute chain rooted at one of them -- through calls too, so
`Gui.getMainWindow().getActiveWindow().getViewer()` is one chain.  A
chain is bucketed by the longest matching prefix in the table `RULES`,
which IS sec 4 written down as an allowlist: `subset` (the U7
compatibility module, no edit), `U1`..`U6` (one protocol op each, with
the op named), `U4.doCommand`, and `unmapped`, the residue.  View
methods reached through a stored object (`self.view.addEventCallback`)
are matched by name from a short list.  Class bases count
(`class X(QtGui.QWidget)`), view-provider hook methods are counted
per class, and `.ui` files are parsed for their widget classes.  A
per-file verdict follows from the buckets: `clean`, `no-edit`
(subset + U1/U2/U4 ops only), `form` (U3), `scene` (U5), `tool` (U6),
`RESIDUE` (unmapped).  `--list FILE` prints every use with its line
and op; `--all`, `--no-edit`, `--ui`, `--json` give the full work list,
the no-edit list, the `.ui` subset and a machine-readable dump;
`--self-test` checks the classifier on a fixture.

### 8.1 The numbers (this tree)

    root      py   importing PySide   pivy   FreeCADGui   .ui
    Draft    243        119            20        97        19
    BIM      224         85            19       148        49

    bucket         uses   files    what it is
    -------------  -----  -----    -----------------------------------
    subset         2187    189     translate, icons, enums, one-line dialogs, defer
    U1              335    169     addCommand, addPreferencePage, workbench queries
    U2              209     54     hints, cursor, updateGui, dialog builders
    U3             1370    125     widget trees, loadUi, Control.showDialog, signals
    U4              846    192     Selection, ActiveView, setEdit, runCommand, addModule
    U4.doCommand    247     47     doCommand / doCommandGui
    U5              470     29     Coin nodes that are annotation primitives
    U6              243     62     Snapper, event callbacks, event classes, trackers
    unmapped        375     56     the residue (8.3)
    hooks           546     78     view-provider hook methods / proxy classes (unchanged)

    verdict        files
    -------------  -----
    clean            189   no Qt, pivy or Gui at all
    no-edit          108   port as they are once the subset and the U1/U2/U4 facades exist
    form              56   U3 only
    tool              27   U6 only
    form+tool         20
    scene, no residue 11   U5 with form or tool, nothing unmapped
    with RESIDUE      56   every combination holding unmapped uses; 7 are residue-only
    (scene, any)      29   U5 in some combination, counted across the rows above

So 297 of 467 files (64 %) need no edit at all, 103 more need only
the form or tool layer, and the residue -- what sec 4 did not
anticipate -- is 375 uses in 56 files, 88 of them one Qt class.

### 8.2 What G1 needs: Draft's App side is already GUI-free

    package                 files   verdicts
    ----------------------  -----   ---------------------------------------
    Draft/draftobjects        28    no-edit 24, clean 4 (194 QT_TRANSLATE_NOOP, nothing else)
    Draft/draftgeoutils       18    clean 17, no-edit 1 (one getViewDirection)
    Draft/draftfunctions      20    clean 20
    Draft/draftmake           31    clean 29, no-edit 2 (3 QTimer.singleShot, 2 getViewDirection)
    Draft/draftguitools       66    tool 24, no-edit 13, form+tool 10, ... (the GUI)
    Draft/draftviewproviders  23    form 7, scene 8, ...
    BIM/(top-level Arch*.py)  53    clean 13, no-edit 8, form 11, form+scene 11, ...
    BIM/bimcommands           83    no-edit 42, form 15, form+RESIDUE 10, form+tool 8, ...
    BIM/nativeifc             21    clean 9, form 5, no-edit 4, ...

Draft's App side (97 files) reaches the GUI layer through exactly three
names: `QT_TRANSLATE_NOOP` (a no-op the subset provides), `QTimer.
singleShot` (defer) and `getViewDirection` (already behind `App.GuiUp`
guards).  G1 for Draft is therefore the Part facade and the package
loader, nothing GUI-shaped.  BIM is different in structure, not in
kind: each `Arch*.py` holds the object class, its view provider and
often a task panel in one file (`ArchComponent.py`, `ArchSite.py`,
`ArchStructure.py`, `ArchWindow.py` are the heaviest), so loading
BIM's App side into the guest means either splitting those files or
loading them whole with the U7 subset present and the view-provider
classes importable but unused.  The second is cheaper and is what G1
should do; the split can follow the G2-G4 rewrites file by file.

### 8.3 The residue, by theme (what sec 4 missed)

    uses  files  theme
    ----  -----  --------------------------------------------------------------
      99     8  model/view: QStandardItem (88) / QStandardItemModel / QTreeView
      63    15  icon composition and textures: QImage, QPainter, QPen, QBrush,
                QLinearGradient, QPixmap.fromImage
      23    10  QStyledItemDelegate (custom cell editors and painting)
      20     4  Inventor-string shape copy: shape.writeInventor -> SoInput ->
                SoDB.readAll (the ghost trackers, ArchAxis)
      17     6  Qt I/O types: QByteArray, QBuffer, QIODevice (XPM icon strings)
      16     7  Qt event types: QEvent.KeyPress/FocusIn/..., QKeyEvent, QMouseEvent
      13     8  FreeCAD Coin nodes by name: SoType.fromName("SoBrepEdgeSet") x9,
                SoBrepFaceSet, SoBrepPointSet, SoDatumLabel, SoFCSelection,
                SoSkipBoundingGroup
      11     3  addon state hung on the Gui module: Gui.IFC_WBManipulator,
                Gui.IFC_saveshortcut, Gui.BIMSetupDialog, Gui.BIMPreflightDone
       9     5  filesystem: QFile, QDir, QFileInfo, QStandardPaths
       9     5  style/palette: QApplication.style().standardIcon, palette colors
       8     8  QDesktopServices.openUrl
       8     1  rich text: QTextCharFormat, QTextCursor, QSyntaxHighlighter (ArchReport)
       7     5  scene graph handle: ActiveView.getSceneGraph()
       6     3  QDockWidget (BimViews, BimTutorial)
       5     3  camera nodes: SoPerspectiveCamera/SoOrthographicCamera
                (OfflineRenderingUtils)
     <=4        each: MDI-area walk to find the 3D view (WorkingPlane), Quarter
                viewer handle, live-import switches (this fork), synthetic
                events, QSvgWidget, font metrics, status bar, completer, custom
                Signal/Slot, SoGetBoundingBoxAction, SoRayPickAction,
                SoSearchAction, SoGetMatrixAction, SoOffscreenRenderer,
                SoDirectionalLight, showPreferences, mainWindowClosed

Reading of it.  The residue is not "the rest of Qt"; it is five
things, and three of them are one design decision each:

- **A1 -- U3 needs a model op.**  BIM's IFC properties, IFC elements,
  materials, layers and schedule panels are `QStandardItemModel` trees
  with `QStyledItemDelegate` cell editors (combo cells, quantity cells,
  color cells): 122 uses in 11 files, the largest single item.  `.ui`
  has no model; a `QTreeView` in a `.ui` needs one bound to it.  U3
  gains `model.set(rows)` (typed cells: text, number, quantity, choice,
  color, check; editable flags; roles for hidden data) and
  `model.changed` events -- the tree/table shape of the same
  model-sync idea, and what the delegates were doing by hand becomes
  a cell type.  `QDockWidget` (6) is the dock placement U3 already
  planned; it is in the residue only because the subset does not
  provide the class.
- **A2 -- icons are composed at run time, not only referenced.**  80
  uses in 17 files with the I/O types counted: color swatches for
  layers and materials (`QImage` + `QPainter`
  ellipse with a gradient, 48x48), overlay badges on IFC view-provider
  icons (`painter.drawImage` of a half-size overlay), and the result
  serialized to an XPM string through `QByteArray`/`QBuffer` because
  `getIcon` returns a string (that is the whole "Qt I/O" theme).  Two
  data-level ops close it: `icon.swatch(color, shape)` and
  `icon.overlay(base, badge)`, both returning an icon reference the
  host composes; `getIcon` may return such a reference.  Texture
  patterns (`view_layer`, hatch) are U5 image data.
- **A3 -- the ghost trackers copy shapes through Inventor text.**
  `shape.writeInventor()` -> `SoInput` -> `SoDB.readAll` in the ghost,
  b-spline and edit trackers and in `ArchAxis` (33 uses in 10 files
  with the by-name nodes): a U6 tracker (sec 4
  lists "ghost of a shape") that in the protocol is a shape-reference
  primitive (`shape: <object or subelement>` rendered by the host from
  its own cache), not a copied mesh.  The FreeCAD Coin nodes created by
  name (`SoBrepEdgeSet` x9, `SoFCSelection`, `SoSkipBoundingGroup`)
  are the same need seen from the other side -- selectable geometry
  primitives -- and `SoDatumLabel` is the C++ dimension of decision 3.
- **A4 -- U2 gains three small ops**: `openUrl` (8 files, a grant like
  the file dialog), `status(message)` for the status bar, and a theme
  query (`standardIcon`, palette colors -- 9 uses) that the DOM
  renderer needs anyway.  `showPreferences` is one more.
- **A5 -- what stays on the island by design**: Qt event filters and
  synthetic events (16 + 3, in the Draft toolbar and BIM's dialogs),
  `QSvgWidget`, rich text (`ArchReport`), `QMdiArea` walks (rewrite
  to `view.active`), filesystem and Quarter handles, the offscreen
  renderer (`OfflineRenderingUtils`: a `view.saveImage` op or a
  headless render service), and BIM's habit of hanging state on the
  `Gui` module (11 uses; guest module state instead).  None of these
  are protocol gaps; they are rewrites in the files that own them.

### 8.4 The `.ui` subset the U3 renderer must cover

68 files, 41 widget classes.  By widget count: `QLabel` 409,
`Gui::PrefCheckBox` 121, `QPushButton` 118, `QGroupBox` 93,
`QCheckBox` 51, `QComboBox` 45, `QWidget` 44, `Gui::InputField` 36,
`Gui::PrefLineEdit` 34, `QDialog` 32, `QSpinBox` 29, `QLineEdit` 28,
`QDialogButtonBox` 26, `Gui::ColorButton` 21, `Gui::PrefComboBox` 20,
`Gui::PrefSpinBox` 18, `Gui::PrefColorButton` 17, `QRadioButton` 15,
`Gui::PrefDoubleSpinBox` 14, `Gui::QuantitySpinBox` 14,
`Gui::PrefUnitSpinBox` 13, `QDoubleSpinBox` 10, `QListWidget` 10,
`QTreeView` 10, `QTreeWidget` 7; the tail is one to four each
(`QScrollArea`, `QSplitter`, `QFontComboBox`, `QTextBrowser`,
`QTableWidget`, `QTabWidget`, `Gui::FileChooser`,
`Gui::PrefFileChooser`, `Gui::PrefRadioButton`,
`Gui::PrefCheckableGroupBox`, `Gui::PrefFontBox`,
`Gui::PrefQuantitySpinBox`, `QListView`, `QTableView`, `QTextEdit`,
`QProgressBar`, `QScrollBar`, `QFrame`).  Sixteen of the 41 are
FreeCAD's own `Gui::` widgets, and the `Pref*` family (preference
pages binding to a parameter path) is a third of all custom widget
instances -- the DOM renderer needs them from day one, as does the
proxy tree (a `Gui::QuantitySpinBox` exposes a quantity, not a
float).  `QTreeView` (10 files) is A1 again.

### 8.5 The work list (top, by weight; `--all` for the rest)

    weight  file                                    what
    ------  --------------------------------------  ---------------------------
       145  Draft/draftguitools/gui_trackers.py     100 U5 uses, A3 residue: the U6 tracker set
       126  Draft/DraftGui.py                       66 U3 hand-built, 15 U6: the toolbar as .ui
       122  BIM/ArchComponent.py                    59 U3 + A1 (IFC properties tree)
       120  BIM/ArchReport.py                       69 U3, rich text residue
       114  BIM/ArchPrecast.py                      108 U3 hand-built widgets
       114  BIM/ArchSite.py                         65 U5 (compass, sun diagram), 31 U3
       108  BIM/bimcommands/BimIfcProperties.py     A1: 24 QStandardItem, delegates
        88  BIM/ArchCoveringGui.py                  U3 + U5 + U6 + icon residue
        87  BIM/ArchMaterial.py                     A1 + A2
        84  BIM/ArchSectionPlane.py                 43 U3, 23 U5 (section plane: C++ candidate)
        83  BIM/nativeifc/ifc_viewproviders.py      A2 (icon overlays)
        78  BIM/bimcommands/BimIfcElements.py       A1
        77  BIM/ArchStructure.py                    54 U3, 20 doCommand, 13 U5
        76  Draft/draftviewproviders/view_dimension.py  70 U5: the C++ dimension
        66  BIM/ArchWindow.py                       66 U3 (form only)
        65  BIM/ArchAxis.py                         23 U5, A3

Verified: `--self-test` on a 25-use fixture (aliases, lazy imports,
class bases, chains through calls, bare view methods, the `()`
construction rule); the survey numbers of sec 1 reproduce (243/224
files, 19/49 `.ui`, 120/87 files importing PySide within one of the
grep count).  Not a build change: no C++ touched, no suite run.

## 9. G1 sized (2026-09-03): what "Draft's App side in the guest" needs

Sec 6 states G1 in one sentence; `sandbox_gui_lint.py --surface`
(the same tool, the other direction: which members declared in the
Part/Base/App Py XMLs the App side reads or calls, against the
`<Sandbox>` annotations) says what that sentence costs.  Roots: the
97 files of 8.2 plus `DraftGeomUtils.py`, `DraftVecUtils.py`,
`WorkingPlane.py`.

### 9.1 The surface

    module members       distinct   uses   examples
    -------------------  --------  -----   ------------------------------------------
    FreeCAD.*                  16    692   Vector 332, GuiUp 98, ActiveDocument 62,
                                           Placement 59, Console 42, Rotation 42
    Part.*                     32    374   LineSegment 50, Wire 47, Circle 34,
                                           OCCError 32, Edge 30, Face 29, makeCompound 18,
                                           Compound 15, makePolygon 13, Arc 12, ...

    declared members read or called: 187 distinct names, 4231 reads

    type            used  annotated today   heaviest members
    --------------  ----  ---------------   ------------------------------------------
    TopoShape         41   5                Vertexes 225, Length 159, Edges 116, Faces 59,
                                            isNull 53, Wires 39, isEqual 36, scale 31
    GeometryCurve     18   0                toShape 48, FirstParameter 39, value, tangent
    BoundBox          17   0                add 107, Center 55, scale 31, move 14
    TopoShapeEdge     17   0                Curve 167, tangentAt 22, valueAt 22, split 12
    Vector            17   0                sub 112, add 107, x/y 104, normalize 79, cross 63
    DocumentObject    13   2                addProperty 192, Name 89, removeProperty 18
    Rotation          13   0                multVec 65, multiply 58, Axis 33
    Document          12   2                addObject 76, Objects 13, removeObject 13
    Placement         12   0                Base 73, multVec 65, multiply 58, inverse 26
    GeometrySurface   12   0
    ShapeList         11   0                append 241, Shape 149, extend 45
    TopoShapeWire     11   0                OrderedEdges 14, makeOffset 3, makePipeShell 1
    BSplineCurve, BezierCurve, TopoShapeFace  10 each, 0 annotated
    Matrix 9, Geometry 6, PropertyContainer 6 (setPropertyStatus 57,
    PropertiesList 39), the conics, Quantity 3 (Value 92), ... 0 annotated

Annotated members in these XMLs today: 14 in all (TopoShape 5,
DocumentObject 3, ComplexGeoData 3, Document 2, Sheet 1).  The
expression rung needed property reads and a handful of shape queries;
Draft's geometry code needs the geometry API.

### 9.2 What exists and what does not

- **The facade generator and the annotation rule** exist
  (`ExpressionSandbox.md` sec 7.5: absent means DENY, host CPython
  runs only `call` members).  Growing the surface is annotation work
  over the XMLs, generated the same way -- large but mechanical, and
  the security review is the diff of the annotations.
- **The wire has no write.**  `FcxWire.h`: `eval`, `read_prop`,
  `get_attr`, `call`, `get_item`, `len`, `release`, `resolve_alias`,
  `pkg.missing`.  An `execute()` that sets `obj.Shape` needs a
  `write_prop` op under `doc.write.self` (sec 3.2 already reserves it
  for exactly this), and `addProperty` (192 uses, in `onDocumentRestored`
  and `__init__`) is a write too.
- **No module facades.**  `Part.Wire(edges)` is a module-level
  constructor: the guest needs a `Part` module whose 32 names are
  facade ops returning handles, and a `FreeCAD` module with
  `ActiveDocument`, `Console`, `Units`, `GuiUp` (False in the guest).
- **No guest-native geometry values.**  The guest wheel is the C++
  value layer compiled to wasm (`ImageSources.cmake`, `_fcx_image`);
  it has no Python-visible `Vector`/`Placement`/`Rotation`/`Matrix`/
  `BoundBox` classes.  Draft does 1.6 k vector operations on this
  surface (`DraftVecUtils` alone: 132 calls); at one hop each that is
  the chattiness sec 3 rules out, so these five are guest-side
  classes (pure Python, or bound to the wasm value layer), crossing
  the wire as values as they do for expressions today.
- **Rung 2 does not exist.**  `FeaturePython::execute()` calls the
  host Python Proxy (`FeaturePython.h:201`); nothing routes a Proxy
  that lives in a guest.  `ExpressionSecurity.h:119` carries only the
  forward-compatible payload slot.  G1's gate ("recompute the Draft
  test documents with routing ON") is a rung-2 gate: the Proxy class
  in the guest, `execute(obj)` as a bridge call with `obj` a handle,
  `PropertyPythonObject` (the Proxy's pickled state) restored in the
  guest, and the write op above.
- **The loader takes lock-file packages only** (`PyodideHost.md`
  12.4): a wheel not in pyodide's index is refused.  Draft's App side
  as a LOCAL wheel needs a local-wheel source in `install_package`
  (manifest entry with `origin: local` and the file's sha256) or a
  build-time wheel beside `fcx_image` picked up at boot.

### 9.3 The plan: four stages, each with its own gate

- **G1a -- the surface.**  Annotate the 187 + 32 + 16 members as
  `value`/`handle`/`call` per sec 7.5, generate the `Part` and
  `FreeCAD` module facades, add the guest geometry value classes.
  Gate: a geometry parity corpus in the shape of the expression
  corpus gate -- every public function of `draftgeoutils` (220 call
  sites in the App side) run in the guest on handles of the same
  shapes, results equal to native.  No document writes yet.
- **G1b -- the wheel and the loader.**  Build `draft_app` (the 97
  files plus the three modules, with a U7 stub for
  `QT_TRANSLATE_NOOP`/`QTimer.singleShot`) as a wheel in the tree,
  the local-wheel source for `install_package`, boot-time load.
  Gate: `import draftgeoutils` in a fresh guest, the G1a corpus
  passing from the wheel.
- **G1c -- rung 2 for one principal.**  `write_prop`, the
  `FeaturePython` seam (a Proxy whose class lives in a guest is
  executed by a bridge call), Proxy state restore in the guest,
  `addProperty`/`removeProperty`/`setPropertyStatus` as write ops.
  Gate: one Draft Wire object's `execute()` in the guest, the shape
  byte-identical to native.
- **G1d -- the G1 gate as written**: the Draft and BIM test documents
  recomputed with routing ON, byte-identical shapes; `TestDraft` green
  with routing ON.  BIM per 8.2: `Arch*.py` loaded whole with the
  subset present, view-provider classes importable and unused.

G1a and G1b are independent; G1c depends on G1a; G1d on all.  G1a is
the bulk of the typing, G1c the architecture.

### 9.4 Decisions to take before G1a

1. **Where host CPython still runs.**  Annotating the surface as
   `call` puts every `Part.Wire`, `fuse`, `tangentAt` through the
   Python C API on the host at rung 0/1 (`ExpressionSandbox.md` sec
   7.5, second rule).  Rung 1 (generated C++ dispatch) would remove
   that, but it is a separate arc; the proposal is `call` now, rung
   1 later, since document principals never reach these until G1c.
2. **Which guest hosts Draft.**  Draft is `addon:Draft`, so the addon
   guest -- one instance per addon, Proxy state per document inside
   it -- rather than a per-document guest (rung 2 proper).  Proposal:
   the addon guest for G1; the per-document split when rung 2 covers
   document-embedded Proxies.
3. **The geometry value classes.**  Pure Python `Vector`/`Placement`/
   `Rotation`/`Matrix`/`BoundBox` in the guest (simple, slow, exact
   only if the operations are written to match `Base::Vector3d`
   bit for bit) or bindings to the wasm value layer already in
   `_fcx_image` (exact by construction, more generator work).
   Proposal: the wasm layer, because "byte-identical shapes" is the
   gate and floating-point order matters.

## 10. Revision (2026-09-03, evening): the mirror, the widget protocol, the toolkit transition

Three questions from the user after G0, each answered here and each
changing a decision above:

1. "Can we just port pivy over to wasm in full?  I don't see any
   security problem with that."  -- Yes (10.1).  It replaces U5's
   primitives with a mirror and removes the tracker half of U6.
2. "Can we find some Python GUI module that has backends for both Qt
   and imgui, as a transition from Qt to imgui ... to give user Python
   code GUI capability without compromising security?"  -- No such
   module exists; the transition mechanism is the toolkit-neutral
   widget model behind U3, with a Qt renderer now and a Qt-free one
   later (10.3).
3. "Is there any similar design we can reference?" and, on the
   Jupyter widgets + pythreejs pair: "very fitting for us, I'd like to
   grow on it." -- The Jupyter widget protocol becomes U3's wire and
   ipywidgets the guest library (10.2); the prior art is in 10.4.

### 10.1 Decision 6: pivy in the guest, the scene mirrored

Coin and `pivy.coin` are compiled into the guest.  Draft's Python
builds real Coin graphs in the guest's own Coin; the host cannot
render nodes in the guest heap, so the boundary is a **mirror**: the
guest's subgraphs are replicated into the host scene and field changes
are re-sent, coalesced per frame.  The wire is the widget protocol of
10.2 -- a Coin node is a model whose synchronized traits are its
fields (`hold_sync` coalesces a tracker's per-frame changes into one
message), and the model classes are generated from Coin's own field
introspection the way pythreejs generates its classes from three.js
(10.4).  What it buys and costs:

- All 555 pivy uses in Draft and BIM port unedited: the dimension view
  provider (70 uses), the trackers (100), the ghost trackers' Inventor
  strings (`writeInventor` -> `SoInput` in the guest's Coin), all of
  it.  Desktop rendering does not change: the host scene holds the
  same nodes it holds today, so the render cache, the bgfx renderer
  and the scene stream see nothing new.
- **Security is the mirror reader, which we write.**  The user's
  reading is right: Coin inside the guest exposes nothing on the host.
  The reader enforces a node-type allowlist that excludes anything
  carrying a file path or code (`SoFile`, `SoTexture2` by filename,
  `SoImage`, `SoWWWInline`, `SoShaderObject`, `SoCallback`, the VRML
  script and inline nodes), quotas on node counts and inline texture
  bytes, a structured wire of type name plus typed fields rather than
  feeding Coin's Inventor parser untrusted text on the host, and a
  check that a selection node's document/object path belongs to the
  principal's own document.  The sec 4 U5 primitive list survives as
  that allowlist.
- What stays as ops: reads of the HOST scene -- `getSceneGraph`, the
  camera node, pick, bounding-box and search actions on host nodes
  (about 30 uses).  FreeCAD's own node types created by name
  (`SoBrepEdgeSet` x9, `SoFCSelection`, `SoDatumLabel`,
  `SoSkipBoundingGroup`; 13 uses) get guest stand-in classes that
  mirror to the real host types.
- **Build risk is the real cost.**  The Coin fork has no emscripten
  support; its dependencies are Boost headers, OpenGL and X11, with
  GLX and EGL behind options, so a no-render build looks feasible but
  is untested.  Native pivy is 34 MB with symbols; the guest grows by
  roughly 15-20 MB of wasm and its boot time by an amount to measure
  -- acceptable for the addon guest, relevant to per-document guests
  later.  The probe comes first (10.6).
- Consequences: decision 3 (dimension to C++) is **withdrawn**; the
  tracker half of U6 disappears; the snapper's move to C++ (decision
  2) stands on cost -- its per-move work is geometry queries against
  host shapes, not Coin -- but is **deferred**, since it is no longer
  a correctness blocker.

### 10.2 Decision 7: the Jupyter widget protocol is U3's wire, ipywidgets its guest library

The widget protocol is a short versioned spec (comm open, state as
key-value diffs, binary buffers, custom messages) with the core widget
models specified attribute by attribute -- about forty of them -- and
two independent model-side implementations, ipywidgets in Python and
xwidgets in C++.  Since ipywidgets 8 the transport is pluggable: the
`comm` package's `create_comm` / `get_comm_manager` are what
xeus-python and JupyterLite's pyodide kernel replace.  So:

- **Guest side: ipywidgets unchanged** in the package set, plus a comm
  shim of about a hundred lines that carries comm messages over the
  existing bridge as one op.  JupyterLite already runs ipywidgets on
  pyodide, from a Web Worker with no DOM access, which is our browser
  tier exactly.
- **Host side: widget managers we write.**  The Qt manager maps the
  core models to Qt widgets.  A FreeCAD widget module, registered on
  both sides, adds what CAD panels need and ipywidgets' core lacks:
  quantity inputs, a selection input (Fusion's
  `SelectionCommandInput` shape, with dynamic filtering), color
  buttons, and the **tree/table model widget of amendment A1** (typed
  cells: text, number, quantity, choice, color, check; editable
  flags; the delegates become cell types).
- **`.ui` stays the authoring format** (decision 1 amended): the
  guest's `loadUi` parses the `.ui` for object names and widget
  classes and builds the widget tree; the host's manager reads the
  same file for layout (Qt: `uic`, exact; a Qt-free manager: its own
  converter, approximate).  The `.ui` is the layout, the models are
  the state.  The U7 compatibility subset gives the models Qt-flavored
  accessors (`text()`/`setText()` over `.value`) so the 25 `loadUi`
  panels port with little edit; hand-built widget trees are rewritten
  as ipywidgets trees or as `.ui`.
- **Only host-registered model names render.**  ipywidgets' habit of
  loading a widget package's own JavaScript from a CDN is the one
  thing the managers never do: an unknown `_model_module` /
  `_model_name` is the allowlist failing, rendered as a labeled
  placeholder (the server-driven-UI lesson: define the fallback).
- Chattiness: one state message per user event, batched per guest
  call as before; `hold_sync` for bursts.  Caveat: ipywidgets is
  traitlets-based, heavier per attribute than a hand-rolled model;
  JupyterLite shows pyodide carries it.  The `HTML` and `Output`
  widgets are browser-shaped; native managers support a subset and
  say so.
- The escape hatch -- an HTML panel in a sandboxed iframe with
  JSON-RPC over postMessage, which VS Code (webview), Figma (UI
  iframe) and MCP Apps all ended up with -- is **not offered** for
  now; if the browser tier ever needs it, MCP Apps is the shape.

### 10.3 Decision 8: the toolkit transition goes through the managers

No Python toolkit with both a Qt and an imgui backend exists (Dear
PyGui, imgui_bundle and pyimgui are imgui only and immediate-mode;
Toga has the abstract-API shape but neither backend; Slint has its own
renderer, a Qt platform backend and a wasm target, but its
royalty-free license requires the Slint attribution and is GPLv3
otherwise, a poor fit for an LGPL project).  None is adopted.  The
transition is:

1. The Qt manager first (free through `uic`, exact fidelity for the
   68 existing forms).
2. A Qt-free manager over bgfx for the same models, drawn in the 3D
   viewer and identical in the browser viewer: the first Qt-free
   panels on the desktop, and the first concrete step of retiring Qt
   (panels move behind the model; the main-window chrome follows,
   later).  For its renderer, **RmlUi is preferred over Dear ImGui**:
   the ipywidgets `Layout` model is CSS flexbox and grid, RmlUi
   implements flexbox and renders through a renderer you supply
   (bgfx), and it covers the `HTML` widget's subset; imgui would need
   Yoga for layout and a retained walker on top.  Dear ImGui is
   already vendored under the bgfx submodule; RmlUi is not.  The
   choice is taken at the time that manager is built, on a measured
   prototype of each.
3. Addons that insist on direct PySide (or direct imgui) stay on the
   native island, as sec 5 says.

### 10.4 Prior art, mapped onto the capability sets

    design                          pattern                                   backs
    ------------------------------  ----------------------------------------  --------
    VS Code extension host          contribution points (manifest data), thin  U1, U2,
                                    service API, TreeDataProvider (host       U4, A1
                                    renders), webview as escape hatch, remote
                                    extension host = location transparency
    Zed extensions (Wasmtime, WIT)  "the extension describes what to render   U3
                                    as data, Zed renders it natively";
                                    "extensions compose primitives, they
                                    don't draw pixels"; events return data
                                    patches; GPU access rejected
    Figma plugins                   QuickJS compiled to wasm after the Realms  runtime,
                                    shim proved unsafe ("object                network
                                    representations too different" for
                                    confusion attacks); typed document API;
                                    UI iframe; manifest networkAccess
                                    allowedDomains
    Jupyter widgets / JupyterLite   kernel-side models, front-end views,       U3 wire,
                                    state diffs + binary buffers over comm;    browser
                                    several front ends; ipywidgets on pyodide  tier
    pythreejs / xthreejs            a scene graph as widget models, classes    U5 mirror
                                    GENERATED from a per-class config
    xwidgets, euporie               C++ model side of the same protocol;       host
                                    a non-DOM (terminal) renderer of it        managers
    Cash App Redwood + Zipline      Kotlin logic in a QuickJS guest, widget    U3 wire,
                                    protocol generated from schema, native     versioning
                                    renderers; guest/host protocol versioned
    Office.js                       proxy objects queue, one sync flushes;     batching,
                                    scalar vs navigational properties          values/handles
    Fusion 360 command inputs       typed inputs (selection, value with units, FreeCAD
                                    dropdown, table, triad ...), host renders, widget
                                    input-changed / validate / execute events  module
    Onshape FeatureScript           parameters with UI annotations, dialog     same
                                    generated, sandboxed language
    Adaptive Cards, Lyft Canvas,    declarative schema rendered natively per   schema,
    Airbnb Magma                    host; protobuf versioning; unknown         fallback
                                    components need a fallback policy          policy
    Unity UI Toolkit                retained UXML/USS, one renderer for        .ui as
                                    editor and runtime                         authoring
    MCP Apps                        sandboxed iframe + JSON-RPC over           escape
                                    postMessage, "no backchannel"              hatch
    Blender layout API              toolkit-neutral vocabulary, but draw()     why not
                                    per redraw in-process with full access     immediate

### 10.5 The lint remapped onto the decisions

`sandbox_gui_lint.py`'s table now carries the revision: Coin classes,
the Inventor-string reads and the by-name nodes are `U5` "mirror, no
edit"; A1 (model/view, delegates) is `U3` "tree/table model widget";
A2 (composed icons, the XPM round trip) is `U2` "icon.swatch /
icon.overlay"; A4 (openUrl, status, theme query, preferences.show) is
`U2`; dock widgets and completers are `U3`.  The per-file weight no
longer counts mirrored scene code.  Same tree, re-run:

    bucket         first pass (sec 8)   revised      what changed
    -------------  ------------------   ----------   ------------------------------
    subset          2187 /  189 files   2187 / 189   --
    U1               335 /  169          335 / 169   --
    U2               209 /   54          310 /  75   A2 (80) and A4 (21) mapped
    U3              1370 /  125         1504 / 125   A1 (122) and docks mapped
    U4               846 /  192          846 / 192   --
    U4.doCommand     247 /   47          247 /  47   --
    U5               470 /   29          503 /  29   A3 mapped; ports UNEDITED
    U6               243 /   62          243 /  62   --
    unmapped         375 /   56          107 /  33   the residue

    verdict                         first pass   revised
    ------------------------------  ----------   -------
    clean                              189          189
    no-edit (incl. mirror-only)        108          118
    form / tool / form+tool only       103          117
    with RESIDUE                        56           33

Files needing an edit: 170 -> 160; uses needing an edit: U3 + U6 +
unmapped = 1988 -> 1854, of which 1504 are forms.  The scene work
(503 uses, 29 files) is gone from the list; the residue is now:

    uses  files  theme (revised)
    ----  -----  --------------------------------------------------------------
      16     7  Qt event types: event filters, key/mouse events (Draft toolbar, BIM dialogs)
      11     3  addon state hung on the Gui module (BIM: guest module state instead)
       9     5  filesystem: QFile, QDir, QFileInfo, QStandardPaths
       8     1  rich text: QTextCharFormat, QTextCursor, QSyntaxHighlighter (ArchReport)
       7     5  host scene graph handle (getSceneGraph): a U4/U5 query op
       7     4  Qt classes outside the subset: QEventLoop, QFileSystemModel, QStringListModel, QToolTip
       5     4  Coin actions on the host scene (bbox, search, matrix, ray pick)
       5     3  camera nodes built for offscreen rendering (OfflineRenderingUtils)
       4     3  MDI-area walk to find the 3D view (WorkingPlane): view.active instead
       4     4  Quarter viewer handle
       4     2  this fork's live-import switches
      <=3      each: synthetic Qt events, QSvgWidget, font metrics, cursor
               position, custom QObject, viewport region, process handle,
               offscreen renderer, light node, .iv writer, Coin version

Every remaining theme is either a rewrite in the file that owns it
(event filters, rich text, the MDI walk, module-level state) or a
host-scene query op (about 20 uses).  The revised work list is led by
forms, as it should be: `ArchPrecast.py` (108 U3), `ArchReport.py`
(72 U3, rich text), `DraftGui.py` (66 U3, 15 U6), `ArchComponent.py`
(77 U3), `ArchCoveringGui.py`, `ArchWindow.py` (66 U3, form only),
`ArchStructure.py`, `ArchSectionPlane.py`, `ArchGrid.py`,
`BimIfcProperties.py` (42 U3, the A1 tree).  `gui_trackers.py` and
`view_dimension.py`, first and fourteenth before, are off the list.

### 10.6 Roadmap, revised

G0 done, G1 as sized in sec 9, G2 (U1 + U2 + U7) unchanged.  Then:

- **Probe A -- Coin and pivy to wasm.**  Compile the Coin fork with
  emcc, no GL, no threads, no fonts; build `pivy.coin` with the guest
  toolchain (`docs/PyodideHost.md` sec 7); load the wheel in a guest
  and time the boot and a 10 k-node graph build.  Decides whether 10.1
  ships as designed or the guest gets a pure-Python Coin-shaped model
  library instead (same mirror, more typing).
- **Probe B -- the widget protocol end to end.**  `ipywidgets` in the
  package set, the comm shim over the bridge, a host manager
  rendering Button, Text, FloatSlider, Dropdown and VBox in Qt from a
  guest script.  Measures boot cost and per-event latency before any
  generator work.
- **G3 -- U3 over the widget protocol.**  The Qt manager for the core
  models, the FreeCAD widget module (quantity, selection, color,
  tree/table), the `.ui` loader on both sides, the U7 accessors.
  Gate: every Draft/BIM task panel opens from the guest and
  round-trips its fields; the BIM setup and views dialogs work; the
  IFC properties tree edits through the model widget.
- **G4 -- the mirror.**  The generated Coin model classes, the mirror
  reader with its allowlist and quotas, the host-scene query ops, the
  stand-ins for FreeCAD node types, the event stream (Coin events
  serialized to guest callbacks, coalesced per frame).  Gate: the
  Draft test documents render identically (pixel compare on a
  converged scene) with the proxies in the guest; the trackers follow
  the mouse under the GUI rig.
- **G5 -- the snapper**, deferred: the C++ snap engine when its cost
  is measured to matter, tool sessions as in sec 4 U6.
- **G6 -- the switch for Draft/BIM**, unchanged.
- **G7 -- the Qt-free manager** over bgfx (RmlUi or imgui + Yoga, on
  a measured prototype), with no Draft/BIM gate of its own: the
  panels render in the 3D viewer and the browser viewer from the same
  models.  This is the toolkit transition's first shipped step.

Dependencies: G3 needs Probe B; G4 needs Probe A and G3 (the mirror's
wire); G5 needs G4; G7 needs G3; G6 needs all but G7.

### 10.7 Open questions

- Probe A's outcome: real Coin in the guest, or a Coin-shaped model
  library.  Both keep the same mirror and allowlist.
- Whether the mirror reader constructs host nodes through the widget
  manager (one code path for forms and scene) or through a dedicated
  scene manager; leaning to one manager with two model families.
- Versioning of the widget protocol between a shipped guest wheel and
  an older host: Redwood's problem; the protocol carries a version,
  the manager refuses a newer major.
- The browser-only tier (no desktop host): the mirrored subset must
  render in the wasm viewer; today's pivy annotations do not reach it
  either, so it is not a regression, but it is work owed.
- Whether `getIcon` returning a composed icon should be a host op
  (icon.swatch / icon.overlay) or an `Image` model with inline bytes
  rendered by the guest; the op is smaller.

## References

- Electron's `remote` module deprecation (synchronous IPC, leaky
  proxies, security): https://github.com/electron/electron/issues/21408
  and https://nornagon.medium.com/electrons-remote-module-considered-harmful-70d69500f31
- Qt Remote Objects (source/replica property-signal-slot replication):
  https://doc.qt.io/qt-6/qtremoteobjects-index.html
- `docs/ThinClient.md` (DOM chrome and the control channel),
  `docs/ComputeBoundaries.md` sec 6 (the unified protocol),
  `docs/ExpressionSandbox.md` secs 3, 7.5, 8 (principals, generated
  facades, the ladder), `docs/SandboxNetwork.md` sec 11 (the switch).

Prior art of sec 10.4 (all checked 2026-09-03):

- VS Code contribution points, tree views, webviews:
  https://code.visualstudio.com/api/references/contribution-points,
  https://code.visualstudio.com/api/extension-guides/tree-view,
  https://code.visualstudio.com/api/extension-guides/webview
- Zed: extensions with custom rendering (discussion #37270):
  https://github.com/zed-industries/zed/discussions/37270; the
  extension API: https://zed.dev/blog/zed-decoded-extensions
- Figma: an update on plugin security (Realms -> QuickJS in wasm):
  https://www.figma.com/blog/an-update-on-plugin-security/; manifest
  network access: https://www.figma.com/plugin-docs/manifest/
- Jupyter widgets messaging protocol:
  https://github.com/jupyter-widgets/ipywidgets/blob/main/packages/schema/messages.md;
  low-level explanation:
  https://ipywidgets.readthedocs.io/en/latest/examples/Widget%20Low%20Level.html;
  the pluggable comm: https://github.com/ipython/comm and
  https://github.com/jupyter-widgets/ipywidgets/issues/3209
- JupyterLite kernels (ipywidgets on pyodide):
  https://jupyterlite.readthedocs.io/en/stable/howto/configure/kernels.html,
  https://github.com/jupyterlite/pyodide-kernel
- pythreejs generator: https://github.com/jupyter-widgets/pythreejs/blob/master/CONTRIBUTING.md
- xwidgets (C++ model side): https://github.com/jupyter-xeus/xwidgets;
  euporie (terminal renderer): https://euporie.readthedocs.io/en/latest/apps/console.html;
  qtconsole never displayed widgets: https://github.com/jupyter/qtconsole/issues/382
- Cash App Redwood and Zipline:
  https://code.cash.app/native-ui-and-multiplatform-compose-with-redwood,
  https://code.cash.app/zipline, https://github.com/cashapp/redwood/releases
- Office.js application-specific API model (proxy objects, sync):
  https://learn.microsoft.com/en-us/office/dev/add-ins/develop/application-specific-api-model
- Fusion 360 command inputs:
  https://help.autodesk.com/cloudhelp/ENU/Fusion-360-API/files/CommandInputs_UM.htm,
  https://help.autodesk.com/cloudhelp/ENU/Fusion-360-API/files/SelectionCommandInput.htm
- Onshape FeatureScript feature UI: https://cad.onshape.com/FsDoc/uispec.html
- Server-driven UI (Airbnb, Netflix, Lyft):
  https://medium.com/@aubreyhaskett/server-driven-ui-what-airbnb-netflix-and-lyft-learned-building-dynamic-mobile-experiences-20e346265305
- Unity UI Toolkit: https://docs.unity3d.com/6000.3/Documentation/Manual/ui-systems/introduction-ui-toolkit.html
- MCP Apps: https://modelcontextprotocol.io/extensions/apps/overview,
  https://github.com/modelcontextprotocol/ext-apps/blob/main/specification/2026-01-26/apps.mdx
- RmlUi: https://github.com/mikke89/RmlUi (flexbox: PR #257); Yoga:
  https://github.com/react/yoga; Dear ImGui bindings list:
  https://github.com/ocornut/imgui/wiki/Bindings; pyimgui:
  https://github.com/pyimgui/pyimgui
- Slint license: https://github.com/slint-ui/slint/blob/master/LICENSE.md
