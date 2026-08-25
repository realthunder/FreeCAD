# Split views: Blender-style view areas as the default viewer widget

Order (2026-08-24, expanded 2026-08-25): let the user split the current window
into multiple view areas, referencing how Blender does it; make the split view
the DEFAULT viewer widget; each active sub-view can display different content
(one a 3D model view, another a TechDraw page); cover the desktop first, the
wasm viewer as a later milestone.

This document records the survey of the existing code (sec 2-4), the design
(sec 5), and the milestone plan (sec 6).

## 1. What Blender does (the reference)

Blender's window is tiled by a "screen": a set of non-overlapping rectangular
areas that always cover the window exactly. The interaction model, which is
what we are porting, is:

- Every area corner carries an "action zone" (a small hot corner). Dragging
  from it INTO the area splits the area in two -- the drag direction picks
  horizontal or vertical. Dragging from it ACROSS the border into a neighbor
  joins the two areas (the neighbor is consumed, with a dark overlay + arrow
  showing what will be eaten before release).
- Dragging a shared border resizes the areas on both sides.
- Right-clicking a border offers explicit Split / Join menu entries; there
  are also Area menu entries and shortcuts (Blender: Ctrl-Alt-Q quad view,
  Ctrl-Space maximize area).
- Each area hosts an "editor" chosen by a per-area selector (3D Viewport,
  Image Editor, Outliner, Timeline, ...). Editor state is per-area; an area
  remembers the state of editors it previously hosted, so switching away and
  back restores everything.
- Exactly one area is active at a time (the one under the cursor in Blender;
  for us, the focused one) and shortcuts/commands apply to it.

Internally Blender stores the screen as a vertex/edge graph (ScrVert/ScrEdge/
ScrArea), which permits non-tree tilings (e.g. a T arrangement produced by
joining across a split boundary). We deliberately use a binary splitter tree
instead: every tiling reachable by split/resize IS a tree, the only graph-only
configurations are exotic join sequences that Blender itself refuses unless
edges align exactly, and a tree maps 1:1 onto nested QSplitter, which gives us
border-resize, size persistence and child management for free. Join is
restricted to sibling subtrees that share a border, same as Blender's aligned-
edge rule in practice.

## 2. What the legacy code offers (and why it is not the base)

`src/Gui/SplitView3DInventor.{h,cpp}`: `AbstractSplitView` (2006) is one
MDIView holding a flat `std::vector<View3DInventorViewer*>` in nested
QSplitters. It is created only by the orthographic/axonometric view commands,
has a fixed layout decided at construction, no split/join interaction, one
document, 3D-only content -- and its `setupSettings()` disables VBO, render
cache, transparency and navigation-style sync ("Disable VBO for split screen
as this leads to random crashes", a pre-`AA_ShareOpenGLContexts` fear). The
render-cache kill also makes it incompatible with the bgfx renderer, which
requires render-cache mode 3. It stays untouched as the implementation of the
existing commands; the new work does not extend it.

What it does prove: multiple `View3DInventorViewer` instances happily coexist
in one MDIView over one document scene.

## 3. Ground truth: how views are hosted today

Facts the design rests on, with sources:

- MDI hosting: `MainWindow::addWindow()` (`MainWindow.cpp:1514`) wraps any
  `MDIView` (a QMainWindow subclass) in a `QMdiSubWindow`. Activation flows
  `onWindowActivated` -> `setActiveWindow(view)` -> `d->activeView` +
  `Application::Instance->viewActivated(view)`; `MainWindow::activeWindow()`
  just returns `d->activeView` (`MainWindow.cpp:1787`). Nothing requires the
  active view to be a direct QMdiSubWindow child.
- The default 3D view: `Application::slotNewDocument()` calls
  `Document::createView(View3DInventor::getClassTypeId())`
  (`Application.cpp:970`); `createView` (`Document.cpp:3652`) news up the
  `View3DInventor`, seeds camera + view providers, then calls
  `getMainWindow()->addWindow(view3D)` itself (`Document.cpp:3707`).
- Views register with their `Gui::Document`, not with the window system:
  `attachView`/`getMDIViews` (`Document.cpp:3790/3973`). Camera save/restore
  iterates `getMDIViews()` and talks `GetCamera`/`SetCamera` messages plus a
  per-view `<View3D id=...>` property block (`Document.cpp:3419-3492`,
  restore `Document.cpp:2649-2755`). None of that cares how the view widget
  is parented.
- Re-parenting an MDIView out of the QMdiArea is already normal:
  `MDIView::setCurrentViewMode` (`MDIView.cpp:441`) moves views between
  Child / TopLevel / FullScreen.
- The command layer finds the current 3D view via
  `qobject_cast<View3DInventor*>(getMainWindow()->activeWindow())` --
  53 sites. Whatever we build must keep that expression working.
- `View3DInventorViewer` is a QGraphicsView whose viewport is a
  `QOpenGLWidget` (`Quarter/QuarterWidget.cpp:279-291`). GL context sharing
  is process-global (`AA_ShareOpenGLContexts`, `Application.cpp:2390`); the
  ctor share-widget only picks the Coin cache-context id
  (`QuarterWidgetP::findCacheContext`). The viewer's pimpl caches
  `qobject_cast<View3DInventor*>(owner->parent())` and uses it ~20 times
  (per-view render properties, `isBackgroundView()`), so the embeddable unit
  is the whole `View3DInventor`, never a bare viewer.
- bgfx multi-viewer support already exists: one `Render::Renderer` per
  `QOpenGLWidget`, all on one process-wide bgfx device; per-widget
  `BGFXView`s keyed by the widget pointer (`BGFXRendererP.h:2159`), view-id
  granule budgeting with a graceful "close another 3D view" fallback
  (`BGFXRenderer.cpp:1198-1251`, `BGFXFrame.cpp:3774`). Therefore a desktop
  split where every 3D cell owns its own GL widget needs NO renderer work.
- TechDraw: `QGSPage` (the QGraphicsScene) lives on `ViewProviderPage`
  independent of any window (`ViewProviderPage.cpp:99`) and is already
  populated headlessly (`renderPageVg`). The widget `QGVPage` needs only
  `(ViewProviderPage*, QGSPage*, parent)`; its single hard MDI coupling is an
  unchecked `static_cast<MDIViewPage*>(parent)` used for context-menu
  forwarding (`QGVPage.cpp:197,701`). `MDIViewPage` itself adds selection
  sync, printing and message routing -- all of which we want anyway, so the
  hosted unit for a page cell is the whole `MDIViewPage`, symmetric with 3D.
- `View3DInventor::isBackgroundView()` (`View3DInventor.cpp:1081`) decides
  render-target release by walking `getMainWindow()->windows()` and tabbed-
  MDI maximization. Embedded child views are not in that list; the container
  must answer for its children (a hidden splitter pane = background).

## 4. Design decision

One new container MDIView -- the "view area" -- that hosts a binary splitter
tree whose leaves each embed a full child `MDIView`. Content heterogeneity,
message routing, printing, per-view persistence and renderer support all come
from reusing the existing MDIView machinery per cell; the container only owns
layout and gestures. Rejected alternatives:

- Extending `AbstractSplitView` (3D-only, fixed layout, cache-off: sec 2).
- A cell type lighter than MDIView (bare viewer / bare QGVPage): breaks the
  ~20 parent-cast sites in the viewer, loses onMsg/print/selection plumbing,
  and forces a parallel content abstraction that MDIView already is.
- Making the QMdiArea itself splittable: QMdiArea's tabbed mode is the tab
  bar users already have; the split happens INSIDE one tab, like one Blender
  window.

## 5. Architecture

### 5.1 Classes (`src/Gui/ViewArea.{h,cpp}`)

- `Gui::ViewArea : public MDIView` -- the container. One per MDI tab. Owns a
  root `QSplitter` (central widget) whose leaf widgets are `ViewAreaCell`s.
  Tracks `activeCell`. TYPESYSTEM-registered; `getName()` = "ViewArea".
- `Gui::ViewAreaCell : public QWidget` -- one area. A thin frame that hosts
  exactly one child `MDIView*` (its only layout item) plus the corner action
  zones and, later, the content switcher. Draws a 1px active-cell outline.
- `Gui::ViewAreaZone` -- the corner action-zone widget implementing the
  Blender gestures (sec 5.4).

The splitter tree is nested `QSplitter`s: splitting a cell replaces it in its
parent splitter with a new 2-child `QSplitter` (orientation from the gesture)
holding the old cell and a fresh cell; joining collapses the sibling pair back
into one cell. Splitter handles give border-resize natively.

### 5.2 Child view hosting

Cells host whole MDIViews with the container as Qt parent (no QMdiSubWindow),
the same re-parenting `setCurrentViewMode(TopLevel)` already performs. Child
views keep their `Gui::Document` registration, so camera save/restore, view
provider attach, `getMDIViews()` and `sendMsgToViews` all see them unchanged.

Rules:

- The container belongs to one `Gui::Document` (the one whose tab it is);
  child views normally belong to the same document. (A TechDraw page of the
  same document satisfies the order's mixed-content case. Cross-document
  cells are not ruled out by the design -- a cell's child keeps its own
  document pointer -- but are out of scope until needed.)
- The container never appears in `Document::getMDIViews()` as a camera
  carrier: it answers `onHasMsg("GetCamera") == false` and forwards messages
  (sec 5.3), so document camera persistence keeps operating on the children.
- Closing semantics: closing the container closes all children (each through
  its normal `canClose()`/delete path). Closing/joining away a cell deletes
  its child view exactly as closing its MDI window would.

### 5.3 Activation and message routing

- Focus-in anywhere inside a cell (event filter on the hosted view) makes it
  the active cell and calls `getMainWindow()->setActiveWindow(child)`. Thus
  `activeWindow()` returns the CHILD, all 53
  `qobject_cast<View3DInventor*>(activeWindow())` sites, `Document::
  setActiveView`, editing (`Document::setEdit`) and the overlay manager work
  on the focused sub-view with zero changes.
- When the QMdiArea activates the container's tab, `MainWindow::
  onWindowActivated` sees the container; the container immediately forwards
  activation to its remembered active cell's child.
- `ViewArea::onMsg/onHasMsg/print*` delegate to the active child, so global
  commands (ViewFit, Print, ...) hit the focused sub-view. `containsView
  Provider()` ORs over children (the `AbstractSplitView` pattern).
- `View3DInventor::isBackgroundView()` learns one new case: if the view's
  parent chain contains a `ViewArea`, background = the container's own
  background status (its tab is behind) -- a visible cell in the front tab is
  never background, a hidden tab releases render targets for all its cells.

### 5.4 Gestures (Blender parity)

- Corner action zones (~16px, top-right and bottom-left of every cell, cursor
  feedback): drag inward past a threshold splits along the dominant drag
  axis, then hands off to live border-resize until release. Drag outward into
  the sibling cell arms join: the doomed neighbor dims with an overlay arrow
  (a translucent child widget), release executes, Esc cancels.
- Splitter borders: native QSplitter resize; right-click on a handle opens
  Split Horizontal / Split Vertical / Join menu.
- Per-cell menu (small button in the cell's top-left corner, later a full
  content switcher, sec 5.5): Split H, Split V, Maximize/Restore cell (the
  Blender Ctrl-Space behavior: temporarily collapse the tree to one cell,
  restore preserves the layout), Close cell.
- New splits clone the current cell's content: a 3D cell splits into two 3D
  views on the same document via the `Document::createView` path (camera
  copied, no `addWindow`); a page cell splits into a second view of the same
  page. This is Blender's behavior and gives split-then-navigate for free.

### 5.5 Heterogeneous content (the editor selector)

Each cell carries a content menu listing what the document can show:

- "3D view" -- a `View3DInventor` (via a `createView`-without-`addWindow`
  variant, sec 5.6).
- One entry per `DrawPage` in the document -- an `MDIViewPage`. TechDraw's
  `ViewProviderPage` already reuses an existing `QGVPage` and survives MDI
  teardown with the scene intact, so pointing its create path at a cell is
  the localized change: `createMDIViewPage` grows a parent/host parameter,
  the `static_cast<MDIViewPage*>` in `QGVPage.cpp:197` becomes a checked
  cast. A page can be shown in a cell and later re-opened as a plain MDI tab;
  whichever host exists adopts the one scene. First revision: one host per
  page at a time (the scene's item parenting is single-view).
- Future editors slot in by type: spreadsheet views, the Python console, the
  start page, a second document's 3D view -- anything that is an MDIView.

Switching a cell's content swaps the hosted MDIView; the outgoing view is
deleted through its normal close path (Blender-style per-area editor MEMORY
of previous state is explicitly out of scope for the first revision -- the
document model, not the area, is our state).

### 5.6 Making it the default

`Document::createView` is split into "create the view" and "host the view".
The default path (`slotNewDocument`, `slotFinishRestoreDocument`,
`activateView`) then creates a `ViewArea` holding one 3D cell and adds THAT
to the MDI area. A preference (`BaseApp/Preferences/View/UseViewArea`,
default ON per the order) drops back to the bare `View3DInventor` if needed.
Compatibility notes:

- Python `Gui.ActiveDocument.ActiveView` returns the child 3D view's py
  object, as today, because activation resolves to children (sec 5.3).
- Restore of pre-existing documents: extra saved cameras materialize extra
  3D views (`Document.cpp:2693`); with the flag on they materialize as cells
  of the one container instead of extra tabs (each still applies its own
  `SetCamera` + `<View3D>` block).
- The container persists its layout as its own `<ViewArea>` block inside the
  existing per-view property section of GuiDocument.xml: the splitter tree
  with orientation/sizes and, per leaf, the child view's id (3D) or page
  object name. Old files without the block open as a single cell; old
  FreeCAD builds reading new files ignore the unknown view type by the
  existing versioned-restore rules (`Document::RestoreDocFile` skips view
  entries it cannot resolve).

### 5.7 The wasm tier (later milestones)

The browser viewer has one canvas, one renderer, one camera, and a DOM chrome
layer with no layout system (`web/src/panel.ts` is floating panels only;
`docs/ViewerUIResearch.md` fixed the DOM-over-canvas architecture). The split
there is a different mechanism with the same UX:

- DOM grid over the single canvas: a splitter tree of empty DOM cells (the
  chrome owns gestures exactly as on desktop) whose rects are pushed to the
  C++ side.
- Renderer work that desktop did not need: `render()` grows an optional
  viewport rect + camera per sub-view -- i.e. render the resident feeds N
  times per frame with per-view matrices and `bgfx` scissor/viewport into the
  same backbuffer, using per-cell view-id granules (the reservation machinery
  exists, `BGFXRenderer.cpp:1210`). The capture bracket's feed-swap dance is
  NOT the model; sub-views share the resident scene and differ only in
  camera/viewport, which bgfx view ids express natively.
- Page cells reuse `s_pageMode`'s `Page2D::render` on their own view ids --
  the current full-canvas page mode becomes "a layout with one page cell".
- Per-sub-view state (camera, mode) joins the client, not the wire protocol;
  the SceneDump snapshot stays scene-only with its single advisory camera.

## 6. Milestones

- M0 -- container core: `ViewArea` + `ViewAreaCell`, programmatic split/join/
  resize (menu + shortcuts, no drag gestures yet), 3D cells only, activation
  routing, `isBackgroundView` answer, close semantics. Opt-in via a command.
- M1 -- Blender gestures: corner action zones with split/join drags, join
  overlay, border context menu, maximize/restore cell.
- M2 -- heterogeneous cells: TechDraw page hosting (`createMDIViewPage`
  host parameter, checked parent cast), the per-cell content menu.
- M3 -- default-on: `createView` split, preference, layout persistence in
  GuiDocument.xml, restore of multi-camera legacy files into cells.
- M4 -- wasm tier: DOM splitter chrome, per-sub-view render viewports,
  page cells. (Separate design pass on the renderer API when reached.)

Sections above this line are the plan; implementation notes get appended per
milestone as sections 7+.
