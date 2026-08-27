# View placement: choosing where new views open

Order (2026-08-28): now that split views exist (docs/SplitViews.md), design a
user-friendly, configurable way to decide WHERE a newly opened view appears --
covering new documents, additional 3D views, TechDraw pages, spreadsheets, the
CAM simulator, and the rest. Proposed defaults from the order: a new document
opens in its own tab; a new view of the SAME document goes into the last split
if the area already has more than one, otherwise it creates a new split.
Reference what other software does. This document is the design; no code yet.

## 1. Prior art

### 1.1 VS Code (the closest model)

- A newly opened editor lands in the ACTIVE editor group as a tab; the tab's
  position inside the group is `workbench.editor.openPositioning`
  (right-of-active by default; left/first/last selectable).
- "Open to the Side" is a PER-ACTION variant (explorer context menu,
  Ctrl+Enter, Ctrl+click on many openers), not a mode: it creates/reuses a
  group in the direction given by `workbench.editor.openSideBySideDirection`
  (right or down, global).
- `workbench.editor.revealIfOpen`: if the resource is already visible in some
  group, focus that instead of opening a duplicate.
- Special content picks its own sensible target: markdown preview offers
  "Open Preview to the Side", search editors open a tab, diff reuses the
  active group. Users get gestures (drag a tab to any edge to split) rather
  than a rule table.

Lesson: one small set of global defaults + explicit per-action "to the side"
variants + a reuse-if-open rule + drag gestures. No per-file-type matrix.

### 1.2 JetBrains IDEs

Single tab strip per window; "Split Right / Split Down" and "Move to Opposite
Group" are commands on the current tab; "Open in New Window" is a per-action
variant. Navigation results open in the current tab (with a preview-tab
option). Configuration is sparse: tab placement, tab limit, and little else.
Same lesson as VS Code: defaults + explicit variants, not rules.

### 1.3 Blender (our split-view reference)

Blender never spawns areas on its own. The layout is entirely user-built;
opening different content means switching the ACTIVE area's editor type, and
named workspaces (tabs) hold whole layouts. Content flows INTO the layout the
user made; the layout never changes shape behind the user's back. Our cell
content menu already follows this. The placement policy below deliberately
keeps that property: it only ever creates a split when the user's chosen
default says so, and never rearranges existing cells.

### 1.4 CAD packages (deep survey 2026-08-28)

- SOLIDWORKS: classic MDI child windows and NO document tab bar (users have
  requested one for 15+ years); switching is the Window menu / Ctrl+Tab.
  Viewport splits (Single / Two-H / Two-V / Four View) exist WITHIN one
  window, always show the SAME document, and are not even persisted --
  returning to Single View discards the arrangement. Link Views syncs
  pan/zoom across orthogonal panes only.
- Siemens NX: the canonical NAMED layout system -- View > Layout, fixed
  grids L1..L9, a named model view assigned per cell, saved BY NAME in the
  part file, Replace View swaps one cell. NX 12+ adds tabbed multi-window
  with tab tear-off to floating windows.
- AutoCAD: the other named-layout system -- VPORTS viewport configurations
  saved in the DWG (Single, Two V/H, Three x6, Four x3), restorable even
  into paper space. One file tab per drawing; since 2022 a tab can be torn
  off into a floating OS window (SYSFLOATING).
- Creo: one window per object through Creo 12; Creo 13 switched the DEFAULT
  to tabs in one main window, with a global "Open objects in tabs" setting
  and drag-out-to-window -- the most direct prior art for a user-selectable
  tabs-vs-windows policy switch. No viewport split in modeling.
- Fusion 360: one tab per design; a drawing becomes another document tab;
  Multiple Views is a fixed 2x2 quad toggle; zero placement preferences.
- Onshape: bottom tab manager, one active tab per browser window; the
  placement rule is documented verbatim: "A newly created tab is placed
  directly to the right of the currently active tab and is made active
  immediately." No splits at all; multi-view = more browser windows.
- Rhino is the opposite pole: a quad viewport layout IS the default working
  surface (template-defined, stored in the .3dm), with free split/float/
  overlap commands but no named layouts. Our UseViewArea=true single-cell
  default plus cheap split gestures covers that persona without imposing it.

Lessons: (1) documents are tabs/windows and splits show the SAME document --
NO surveyed product tiles different documents in one split, which validates
the policy's never-cross-documents rule (sec 3.3). (2) A newly opened
document is always activated; nothing opens in the background. (3)
Configuration is thin everywhere: a global preference plus per-action
gestures; no product ships a per-document-type placement rule table. (4)
Tab tear-off to a floating window is the modern convergent feature (AutoCAD
2022, NX 12, Creo 13, Blender) -- our Floating target and the P3 drag
gestures line up with it. (5) Simulation-result COMPARISON converges on a
bounded n-up of at most 4 panes with a camera-sync toggle (SOLIDWORKS
Compare Results, Fusion Compare, NX L2/L4 post views).

### 1.5 Editors and browsers

- vim: global `splitbelow`/`splitright` set the split direction; per-command
  modifiers (`:vsplit`, `:topleft split`) override. Direction-as-a-preference
  is expected by this audience.
- emacs `display-buffer-alist`: the cautionary tale. A fully general
  per-buffer-name rule engine with action lists -- famously the most
  complained-about configuration surface in emacs. We explicitly do NOT
  build a rule engine; the advanced layer is a flat per-view-type override
  map in the parameter editor, nothing more.
- Browsers: a link-opened tab inserts NEXT TO its opener; a fresh Ctrl+T tab
  goes to the end. Placement relative to the OPENER is the pattern to copy:
  our "same document" category is exactly "related to opener".

## 2. Ground truth (what the code does today)

Full survey 2026-08-28; the facts the design rests on:

- Every view becomes an MDI tab through ONE choke point,
  `MainWindow::addWindow()` (MainWindow.cpp:1514). No caller anywhere makes a
  placement decision beyond calling it; ~15 opener sites (below) all
  hard-code "new tab".
- `Document::createView()` (Document.cpp:3964) is already split into create
  (`createView3D`, bare) + host (wrap in a single-cell `ViewArea` when
  `UseViewArea`, default ON, then `addWindow`). This is the pattern the
  policy generalizes.
- `ViewArea` (ViewArea.h:174) already provides every placement primitive the
  policy needs: `wrap()` (promote a bare tab), `splitCell()` (with content),
  `setCellView()` (replace a cell's content, refusable via the child's normal
  close path), `detachViewForHosting()`, `areaOf()/cellOf()`, layout
  persistence, and `activeSubView()` so `activeWindow()` always resolves to a
  leaf view.
- Reuse-if-open already holds: every view-provider-backed opener
  (`ViewProviderPage::show`, `ViewProviderSheet::getMDIView`,
  `ViewCAMSimulator::instance`) returns the existing view if there is one.
  The policy therefore governs CREATION time only.
- Opener call sites, by intent:
  - First view of a document: `Application::slotNewDocument`
    (Application.cpp:1047) -> `Document::createView`.
  - Additional same-document views: `Std_ViewCreate` (CommandView.cpp:2573),
    TechDraw `ViewProviderPage::createMDIViewPage`
    (ViewProviderPage.cpp:316), Spreadsheet
    `ViewProviderSheet::showSpreadsheetView`
    (ViewProviderSpreadsheet.cpp:154), legacy Drawing
    (Drawing/Gui/ViewProviderPage.cpp:159), CAM simulator
    `ViewCAMSimulator::instance` (ViewCAMSimulator.cpp:393),
    `ViewProviderTextDocument::doubleClicked`, dependency graph
    (CommandDoc.cpp:614).
  - Utility views, mostly documentless: image view, Python/text editors (4
    sites), browser views (4 sites), Start page, license view, Python
    `Gui.getMainWindow().addWindow`.
- The one existing "move into a split" affordance is `Std_ViewCellShowObject`
  (CommandView.cpp:2752) plus the per-cell content menu -- both pull EXISTING
  content in; nothing controls where NEW content lands.

## 3. Design

### 3.1 The model: categories x targets

Rule 0 (reveal-if-open, VS Code's `revealIfOpen` as an invariant): opening
something that already has a view activates that view wherever it lives --
tab, cell, or floating window. Unchanged from today; the policy runs only
when a view is actually created. One exception, ruled 2026-08-28: with the
Alt inversion held, an already-open view is RELOCATED to the inverted
placement instead of just activated (sec 4.2).

Three CATEGORIES -- few enough to explain in one sentence each, matching how
every surveyed program divides the world:

- Document: the first view of a new/opened document.
- Document view: an additional view of a document that already has one
  (a second 3D view, a TechDraw page, a spreadsheet, the CAM simulator).
- Utility: everything documentless or meta (editors, browser, image view,
  dependency graph, Start page).

Each category has a default TARGET:

- `Tab` -- a new MDI tab (today's behavior everywhere).
- `Split` -- into the document's view area: REUSE the most-recently-used
  cell that hosts a NON-3D view, else SPLIT the active cell. (Ruled
  2026-08-28: a 3D view's cell is never replaced -- 3D views are the
  primary content; pages, sheets and the simulator share the auxiliary
  slot(s). The reuse half is vim/emacs "other window", the split half is
  VS Code "open to the side".)
- `NewSplit` -- always split, never reuse a cell.
- `Floating` -- a top-level window (`setCurrentViewMode(TopLevel)`).

Two content rules sit above the targets (ruled 2026-08-28):

- A non-3D view NEVER opens in place of a 3D view. The reuse step only
  considers cells hosting non-3D content.
- A new 3D view never reuses either -- it always splits (you asked for an
  additional viewpoint, not to lose a page). So reuse is strictly non-3D
  content replacing non-3D content.

Defaults: Document=Tab, Document view=Split, Utility=Tab. `Tab` for
documents matches every CAD package surveyed; `Split` for document views is
the order's proposal and matches the "viewports look at one document"
convention. Document=Split IS offered (ruled 2026-08-28) -- opening new
documents into the current area is allowed, just not the default; see the
cross-document notes in sec 3.3.

### 3.2 Resolution algorithm (the whole policy, in order)

Input: the freshly created view V, its category C, its Gui::Document D (null
for utility), and the opener view O when known (the active view at the time
of the request).

1. Look up the target: per-type override map first (sec 4.3), else the
   category default.
2. Target Tab or category Utility-with-no-document: `addWindow(V)`. Done.
3. Target Floating: `addWindow(V)` then `setCurrentViewMode(TopLevel)`.
4. Target Split/NewSplit: find the host area A = the `ViewArea` containing
   D's active view (fall back: any `ViewArea` holding a view of D; then, if
   `UseViewArea` is on and D's active view sits in a bare tab, `wrap()` it;
   else fall back to Tab).
5. If A has a maximized cell, un-maximize first (content must never land
   invisibly).
6. Target Split, V is non-3D, and A has at least one cell hosting a non-3D
   view: pick the most-recently-active such cell and ask it to adopt V
   (`setCellView`-style; the outgoing child goes through its normal close
   path). Cells hosting 3D views are never candidates, and a 3D-view V
   skips this step entirely. If the chosen child REFUSES to close (unsaved
   editor), fall through to a new split rather than fighting the veto.
7. Otherwise (single cell, or NewSplit, or step 6 refused):
   `A->splitCell(activeCell, direction, V)` where direction comes from the
   split-direction preference (sec 4.1); `Auto` picks the longer side of the
   active cell (requested of VS Code by users, trivially right for us).
8. Activate V (all targets); the opener O stays where it was.

MRU cell tracking (step 6) is one integer counter bumped in
`ViewArea::setActiveCell` -- the only new state the design adds to ViewArea.
Areas restored from file start with no MRU history; the deterministic
fallback is tree order after the active cell.

### 3.3 What the policy never does

- Never moves or closes EXISTING views to make room (only step 6's adopt,
  which is refusable).
- Never changes the layout shape except by the one split the user's chosen
  default asks for.
- The Document-view category never crosses documents: a view of D lands in
  D's area or a tab, never in another document's area. The DOCUMENT
  category may cross by explicit choice (Document=Split, or Alt inversion
  on an open): the new document's first view then lands in the CURRENT
  area, making that area host children of two documents -- which
  SplitViews.md sec 5.2 already permits structurally. Consequence for
  persistence: a `<ViewArea>` layout is saved in ONE document's
  GuiDocument.xml, so a cell whose child belongs to another document is
  skipped by the leaf-token writer and dropped on restore (same per-leaf
  tolerance the simulator cell needs, sec 6).
- Never runs for restore: document restore replays saved `<ViewArea>`
  layouts (Document.cpp:2833) and must stay byte-stable; the policy applies
  to interactively opened views only.

## 4. Configuration and UI

### 4.1 Preferences (`BaseApp/Preferences/View/OpenView`)

- `DocumentTarget` = Tab | Split | Floating (default Tab)
- `DocViewTarget` = Tab | Split | NewSplit | Floating (default Split)
- `UtilityTarget` = Tab | Split | Floating (default Tab)
- `SplitDirection` = Auto | Right | Down (default Auto = longer side)

Exposed on a new "Views" group in the Display preferences page: three combo
boxes and the direction combo, each with a one-line label ("New documents
open in", "Additional views of a document open in", "Utility windows open
in", "New splits go"), plus a static hint line: "Hold Alt while opening to
invert tab/split for that one view." (Ruled: the hint in preferences IS
the discoverability story -- no alternate context-menu entries.)
`UseViewArea` graduates from a hidden parameter to a checkbox on the same
group, and disabling it greys the split choices.

### 4.2 The Alt inversion (the single escape hatch, ruled 2026-08-28)

- Holding ALT while triggering an open (tree double-click, toolbar/menu
  command) inverts Tab <-> Split for that one view. Read via
  `QGuiApplication::queryKeyboardModifiers()` at request time. No
  alternate context-menu entries (ruled); the preferences hint (sec 4.1)
  documents it.
- Already-open + Alt = RELOCATE (ruled): when rule 0 would merely activate
  an existing view, Alt instead moves it to the inverted placement --
  a view in a cell pops out to its own tab, a tabbed view drops into the
  split. Specified as close-and-reopen; implemented as a state-preserving
  move (`detachViewForHosting` + re-place), which is observably the same
  minus the state loss.
- Conflict check (done 2026-08-28): Alt is FREE on the paths that matter.
  Tree `onDoubleClickItem` reads no modifiers today, and the Alt uses in
  `Tree.cpp` sit elsewhere: Alt+click on an item's ICON routes to
  `ViewProvider::iconMouseEvent` (Tree.cpp:2938), Alt suppresses the tree
  context menu (Tree.cpp:1652), and Alt during tree DRAG means LinkAction
  (Tree.cpp:3192) -- none fire on a plain double-click of the item label.
  Caveats to carry into implementation: (a) double-clicking the item's
  icon (not label) with Alt held is claimed by `iconMouseEvent` first;
  (b) a command whose own keyboard shortcut contains Alt must suppress
  inversion for keyboard activation; (c) some Linux window managers grab
  Alt+drag (KDE window move) -- plain Alt+clicks still reach the app, but
  this is worth a release-note line.
- The existing `Std_ViewCellShowObject` and the cell content menu remain
  the "pull it in afterwards" path and are untouched.

No per-view-type override settings (ruled 2026-08-28): the categories, the
non-3D reuse rule and the Alt inversion are the whole configuration
surface. Spreadsheets follow DocView=Split like pages -- they share the
non-3D slot.

### 4.3 Later gestures (out of first scope, recorded)

- Drag an MDI tab and drop it onto a cell to move that view into the split
  (VS Code drag-to-split; the machinery is `detachViewForHosting` +
  `setCellView`, only the drop target UI is new). Complements, not
  replaces, the policy.
- Named cell layouts (NX Layouts / AutoCAD VPORTS are the prior art):
  save/recall a ViewArea arrangement by name. `layoutString` is already the
  serialization; only naming, storage scope (per document vs global) and UI
  are missing.
- Linked navigation across cells (SOLIDWORKS Link Views, the sim-compare
  camera sync every vendor ships): a per-area toggle synchronizing the
  cameras of its 3D cells.

## 5. Call-site conversion

One new helper, proposed home `src/Gui/ViewPlacement.{h,cpp}`:

    namespace Gui::ViewPlacement {
        enum class Category { Document, DocView, Utility };
        void place(MDIView *view, Category cat, Document *doc,
                   MDIView *opener = nullptr);
    }

`Document::createView` keeps its create/host split; the host half becomes
`place(view, Document, ...)`. Conversion by category:

- Document: Document.cpp:3979-3990 (the UseViewArea branch collapses into
  the policy).
- DocView: CommandView.cpp:2573 (Std_ViewCreate),
  ViewProviderPage.cpp:344, ViewProviderSpreadsheet.cpp:163,
  Drawing/Gui/ViewProviderPage.cpp:169, ViewCAMSimulator.cpp:405+419,
  ViewProviderTextDocument.cpp:94, CommandDoc.cpp:620 (dependency graph --
  category call pending, sec 8).
- Utility: the editor/browser/image/start sites keep plain `addWindow` in
  the first milestone (their default is Tab anyway) and convert only when
  someone wants the pref to affect them; `Gui.getMainWindow().addWindow`
  stays raw `addWindow` forever (Python callers place explicitly).

`ApplicationPy sCreateViewer` (Gui.createViewer) stays outside the policy:
documentless by design, callers manage it.

## 6. The CAM simulator (the case that motivated this)

`ViewCAMSimulator::instance` currently opens the simulator as its own tab,
which hides the document view -- the doc-view attach work (CAMSimRenderPort
sec 11.9) then shows the simulation inside the document's 3D view, but the
user cannot SEE both at once without manually splitting. Under this design
the simulator is category DocView: with the default `Split` it lands beside
the document's 3D view in the same area -- simulator controls on one side,
the attached document view carving on the other, which is the intended
showcase of both features together.

Specifics:

- `instance()` passes the job's `Gui::Document` (it already requires one);
  the clone-for-another-document path re-places through the same call.
- A simulator cell is NOT persistable: `layoutString` leaf tokens name view
  providers or persistent 3D views, and the simulator has neither. The leaf
  resolver already returns null for unknown tokens; the required behavior
  change is per-leaf tolerance in `applyLayout` (drop the cell, renormalize
  sizes) instead of `deleteSelf` of the whole area at Document.cpp:2901 --
  a small, separately testable change.
- The `GuiDisplay` button row and the `Dummy3DViewer` navigation work
  unchanged in a cell (the stacked widget does not care about its parent);
  half-width is fine for the button row (~8 small buttons).

## 7. Milestones

- P0 -- policy core: `ViewPlacement::place`, MRU cell tracking, prefs read
  (hidden), Document + DocView call sites converted, per-leaf-tolerant
  `applyLayout`. Behavior change visible: pages/sheets/sim/second-3D open
  into splits by default.
- P1 -- configuration surface: the Display preferences group (incl.
  UseViewArea checkbox and the Alt hint line).
- P2 -- the Alt inversion, including relocate-when-already-open and the
  keyboard-shortcut suppression caveat.
- P3 (unscheduled) -- tab-onto-cell drag; Document=Cell (cross-document
  cells); wasm-tier equivalent once the browser has layout chrome worth
  driving (SplitViews.md sec 5.7).

## 8. Rulings (2026-08-28) and what stays open

Ruled by the user:

1. A non-3D view never opens in place of a 3D view: reuse targets the
   last-used NON-3D cell, else a new split (sec 3.1).
2. Document=Split is offered, just not the default (sec 3.1, 3.3).
3. Alt is the inversion modifier; conflicts checked (sec 4.2).
4. No per-view-type settings; the Alt inversion is mentioned in the
   preferences page rather than via context-menu variants (sec 4.1, 4.2).
5. Inversion on an already-open view closes it and reopens it on the other
   side (specified as a state-preserving relocate, sec 4.2).

Ruled 2026-08-28 (second round): the dependency graph is category
DocView, and a new 3D view always SPLITS (confirmed). Nothing remains
open; P0 is a go.

## 9. P0 implementation notes (2026-08-28)

Landed as `0985c292a5` (policy) + `10af0c1f8f` (lifetime fixes). What
the plan did not know:

- `applyLayout` per-leaf tolerance ALREADY existed: the LayoutParser
  skips an unresolved leaf, flattens single-child groups, and returns
  false only when NOTHING resolved (the `deleteSelf` at
  Document.cpp:2901 fires only then). The sec 6 work item was free.
- `splitCell` keeps the ORIGINAL cell active on purpose (gesture
  splits stay where the user is), so the policy needed its own
  activation hook: `ViewArea::activateCellOf(view)`. The MRU state is
  one int stamp per cell bumped in `setActiveCell`.
- Restore does not rely on any `isRestoring` flag: the legacy
  multi-camera branch (Document.cpp:2716) calls the new
  `ViewPlacement::placeTab` directly, and `hostArea` refuses an area
  with no `parentWidget()` -- layout restore hosts views into a
  ViewArea it is still assembling and adds it to the MDI area only
  afterwards, so a page's `show()` firing mid-parse cannot join (or
  mutate) the half-built tree.
- The probe (5 steps under xvfb: doc tab / 3D split / sheet split /
  dep-graph reuse / second doc tab) exposed a PRE-EXISTING lifetime
  race that `setCellView` makes easy to reach: `MDIView::closeEvent`
  detached the view from the document's list but left `_pcDocument`
  set, counting on the deferred destructor to finish -- a document
  closed in the same event-loop turn left the destructor's `onClose`
  walking a freed document. Fixed by finishing the detach at
  closeEvent time, plus a null-parent guard in
  `MainWindow::removeWindow` for views replaced out of a cell
  (parentless, delete pending).
- Not yet probed: the CAM simulator's split placement (needs a Job
  document; the one-line conversion in `ViewCAMSimulator::instance` is
  identical in kind to the sheet/page ones, but the simulator's
  stacked widget in a half-width cell deserves an eyeball before the
  workstream closes).

## 10. P1 implementation notes (2026-08-28)

The configuration surface of sec 4.1, live: a "Views" group at the top of
Display -> UI, holding the `UseViewArea` checkbox, the three target combos,
the split direction combo and the Alt hint line.

- **A new generated params module**, `Gui/OpenViewParams.py` ->
  `OpenViewParams.h/.cpp` (cog, like ViewParams/ExprParams). It owns the
  four `View/OpenView` parameters, and `ViewPlacement.cpp` now reads them
  through its accessors instead of `GetASCII` with local default strings.
  That is the point of the module: the page writes and the policy reads the
  same generated definition, so the words cannot drift apart. `UseViewArea`
  belongs to `View`, not `View/OpenView`, so it was declared in
  `ViewParams.py` -- one param per path, which is what a params module is.
- **The stored value is the WORD, never the position.** params_utils'
  `ParamComboBox` stores the item index, which would make the parameter
  meaningless on any reorder and unreadable to the policy. The local
  `ParamTargetCombo` proxy instead sets each item's data to its ASCII value
  and sets the widget's `prefType` property to `QByteArray` -- the switch
  that makes `Gui::PrefComboBox::save/restorePreferences` use item data as
  ASCII (PrefWidgets.cpp). The same proxy emits the hint label, so it is
  translated by the generated `retranslateUi` like everything else.
- **The greying**, hand-written in `DlgSettingsUI::init()`: unchecking
  `UseViewArea` disables the split direction row and the `Split`/`NewSplit`
  ITEMS of the three target combos (via the model's item flags) while
  leaving `Tab`/`Floating` selectable. The choices stay visible because
  they are what the checkbox buys.
- **A generator bug surfaced on the way** and is fixed separately:
  `ParamShortcutEdit` inherited `ParamString`'s QString-wrapped default
  expression, but `AccelLineEdit::setDisplayText` takes the `std::string`.
  Any regeneration of a page carrying one (here: Expression's
  `EditorTrigger`) emitted code that does not compile -- which is why the
  checked-in file disagreed with its own generator.
- Verified by an Xvfb probe (`probe_prefs.py`, this session's scratchpad):
  the four combos carry the expected ASCII data and translated texts, the
  defaults are Tab/Split/Tab/Auto, the hint line is present, unchecking
  greys exactly the split items and rechecking restores them, accepting the
  dialog stores `DocViewTarget=Tab` as a word, and the policy then opens a
  second 3D view as a new tab -- and splits again once the value goes back.
- P2 (the Alt inversion itself) is still not built; the hint line announces
  it, which was the ruling.

## 11. P2 implementation notes (2026-08-28)

The Alt inversion of sec 4.2, live. Holding Alt while something opens
swaps Tab and Split for that one view; holding it while REVEALING a view
that is already open moves that view to the inverted placement instead.

- **The modifier is read from the X server, not from an event**:
  `QGuiApplication::queryKeyboardModifiers()` at request time. An opener
  is usually several signals away from the click or keystroke that
  started it, and the event's own modifiers are long gone by then.
- **Floating is not part of the inversion.** It is a different axis, and
  a user who asked for floating windows did not ask for a tab.
- **The relocate is a move, not a close-and-reopen** (sec 4.2 allowed
  either): `ViewArea::detachViewForHosting` then re-place. Out of a cell
  it goes to a tab; out of a tab it goes into the document's area. Two
  details matter. The emptied cell is closed only AFTER the view has its
  new home -- closing it first can close the whole area, leaving the
  document momentarily with no view at all. And the inbound direction
  uses `NewSplit`, never `Split`: a move must not evict, and thereby
  close, whatever sits in the cell the reuse step would have picked.
- **Reveal has to know whether the view is new.** The openers that
  reveal (TechDraw and Drawing pages, spreadsheets, text documents) call
  `ViewPlacement::reveal(view, doc, alreadyOpen)` and pass false for a
  view the policy has just created -- that one was placed with the
  inversion already applied, and inverting it again would only undo it.
- **Caveat (b) is honored in one place**: `Command::invoke` wraps the
  call in `ViewPlacement::SuppressAltInversion` when the invocation came
  from an action whose own shortcut carries Alt. Alt is then down
  because the user pressed that shortcut.
- Verified by an Xvfb probe (`probe_alt.py`, this session's scratchpad)
  that holds Alt **for real** through XTEST -- the policy queries the
  server, so a synthesized Qt event would not be seen. All eight legs
  pass: with the preference on Split a plain open splits and an Alt open
  takes a tab; with it on Tab the two swap; an open spreadsheet
  relocates out of its cell to a tab and back into a cell, the same view
  object surviving both moves; a plain reveal moves nothing; a command
  given an `Alt+Y` shortcut does NOT invert while the same command with
  `Ctrl+Shift+Y` does.

Two notes for whoever probes this next. The preference dialog's OK
writes EVERY page's widgets, so a probe that opens it leaves explicit
values in the real `user.cfg` -- and an exit-time abort (WSLg) can lose
the probe's own cleanup, so a later probe silently runs against the
value the earlier one left. Set the preference each leg depends on
rather than trusting the ambient one. And PySide hands back an
invalidated wrapper once a new widget lands on a freed address, which
makes `findChildren` raise from the inside where no per-item filter can
help: ask a view where it lives by walking its own ancestors instead of
scanning the widget tree.
