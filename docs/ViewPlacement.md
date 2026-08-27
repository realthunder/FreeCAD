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

### 1.4 CAD packages

- SolidWorks / classic MDI CAD: every document (part, assembly, drawing) is
  its own window; making a drawing from a part opens a new window. Viewport
  splits (two/four view) exist WITHIN a window and always show the same
  document. Windows are managed via a Window menu.
- NX: named viewport "layouts" (L1..) per window -- again same-document.
- Fusion 360: one tab per design across the top; a drawing becomes another
  document tab.
- Onshape: one graphics area, a bottom tab manager listing Part Studios,
  Assemblies and Drawings of ONE document; switching tabs switches the
  graphics area; "open in new browser tab" is the escape hatch.

Lesson: CAD users universally expect DOCUMENTS to be tabs. Splits/viewports
are for looking at one document from several angles at once. Our proposed
defaults match this exactly.

- Rhino is the exception worth noting: a fixed 4-viewport layout is the
  DEFAULT working surface. Our UseViewArea=true single-cell default plus
  cheap split gestures covers that persona without imposing it.

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
when a view is actually created.

Three CATEGORIES -- few enough to explain in one sentence each, matching how
every surveyed program divides the world:

- Document: the first view of a new/opened document.
- Document view: an additional view of a document that already has one
  (a second 3D view, a TechDraw page, a spreadsheet, the CAM simulator).
- Utility: everything documentless or meta (editors, browser, image view,
  dependency graph, Start page).

Each category has a default TARGET:

- `Tab` -- a new MDI tab (today's behavior everywhere).
- `Split` -- into the document's view area: REUSE the last-used other cell
  if the area already has more than one, else SPLIT the active cell. (The
  order's proposed default for document views; the reuse half is vim/emacs
  "other window", the split half is VS Code "open to the side".)
- `NewSplit` -- always split, never reuse a cell.
- `Floating` -- a top-level window (`setCurrentViewMode(TopLevel)`).

Defaults: Document=Tab, Document view=Split, Utility=Tab. `Tab` for
documents matches every CAD package surveyed; `Split` for document views is
the order's proposal and matches the "viewports look at one document"
convention.

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
6. Target Split and A has more than one cell: pick the most-recently-active
   cell that is not the active cell and ask it to adopt V
   (`setCellView`-style; the outgoing child goes through its normal close
   path). If the child REFUSES to close (unsaved editor), fall through to a
   new split rather than fighting the veto.
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
- Never crosses documents: a view of D lands in D's area or a tab, never in
  another document's area. (Cross-document cells stay possible manually and
  as a future Document=Cell option; see SplitViews.md sec 5.2.)
- Never runs for restore: document restore replays saved `<ViewArea>`
  layouts (Document.cpp:2833) and must stay byte-stable; the policy applies
  to interactively opened views only.

## 4. Configuration and UI

### 4.1 Preferences (`BaseApp/Preferences/View/OpenView`)

- `DocumentTarget` = Tab | Floating (default Tab; Split intentionally not
  offered until cross-document cells are in scope)
- `DocViewTarget` = Tab | Split | NewSplit | Floating (default Split)
- `UtilityTarget` = Tab | Split | Floating (default Tab)
- `SplitDirection` = Auto | Right | Down (default Auto = longer side)

Exposed on a new "Views" group in the Display preferences page: three combo
boxes and the direction combo, each with a one-line label ("New documents
open in", "Additional views of a document open in", "Utility windows open
in", "New splits go"). `UseViewArea` graduates from a hidden parameter to a
checkbox on the same group, and disabling it greys the split choices.

### 4.2 Per-action variants (the escape hatch, VS Code-style)

- Context menus that open views grow an alternate entry only where the
  default is the other one: a TechDraw page's menu shows "Open in New Tab"
  when the default is Split (and "Open in Split View" when the default is
  Tab); same for spreadsheets and `Std_ViewCreate`'s menu entry.
- Modifier inversion: holding Ctrl while double-clicking (or activating the
  open action) inverts Tab <-> Split for that one open. Read via
  `QGuiApplication::keyboardModifiers()` at request time; advertised in the
  entries' tooltips.
- The existing `Std_ViewCellShowObject` and the cell content menu remain the
  "pull it in afterwards" path and are untouched.

### 4.3 Per-type overrides (advanced, parameter editor only)

A flat map `BaseApp/Preferences/View/OpenViewByType`: key = view type name
(`TechDrawGui::MDIViewPage`, `SpreadsheetGui::SheetView`,
`ViewCAMSimulator`, ...), value = a target name. Looked up before the
category default. No GUI beyond the parameter editor -- this is the entire
"rule engine", kept deliberately at emacs-lesson distance from the defaults.
It exists precisely for calls like "spreadsheets are wide, I want them in
tabs" without demoting the category default for pages and the simulator.

### 4.4 Later gestures (out of first scope, recorded)

Drag an MDI tab and drop it onto a cell to move that view into the split
(VS Code drag-to-split; the machinery is `detachViewForHosting` +
`setCellView`, only the drop target UI is new). Complements, not replaces,
the policy.

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
  UseViewArea checkbox), per-type override map honored.
- P2 -- per-action variants: alternate context-menu entries + Ctrl
  inversion.
- P3 (unscheduled) -- tab-onto-cell drag; Document=Cell (cross-document
  cells); wasm-tier equivalent once the browser has layout chrome worth
  driving (SplitViews.md sec 5.7).

## 8. Open questions (for the user)

1. Spreadsheets: keep them in the DocView=Split default (half-width sheets
   can be cramped), or pre-seed `OpenViewByType` with SheetView=Tab? My
   recommendation: keep Split, no pre-seed -- one policy, one story, and the
   per-type override exists for those who disagree.
2. Dependency graph: DocView (splits beside the model, my lean -- it is a
   read-alongside view) or Utility (tab)?
3. Is Ctrl the right inversion modifier, given Ctrl+click selection
   conventions elsewhere in the tree? Alt is the alternative.
4. Should Std_ViewCreate ALWAYS split (NewSplit) rather than reuse the last
   cell? The order said "last split if more than one" -- confirmed as the
   default for all DocView opens, or intended only for pages/sim?
