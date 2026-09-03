# GUI for sandboxed Python: the FreeCAD UI protocol, not a PySide shim

Status as of **2026-09-03**: discussion draft, nothing built.  Written
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
- **U3 Forms (model-sync).**  The form language is **Qt Designer
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
- **U5 View providers and the annotation scene.**  The proxy hook set
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
  host.**  `Gui.Snapper` becomes a C++ snap engine (endpoint, midpoint,
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

Next: G1 (sec 8.5 says what it needs).

## 8. G0 results: the linter and what it found (2026-09-03)

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
