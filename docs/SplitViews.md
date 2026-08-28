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
- Hovering the per-cell menu button, or any splitter handle, also reveals
  the corner zones (sec 18): the visible chrome is what points at the two
  invisible ones.
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
  with orientation/sizes and, per leaf, the child view's persistent name
  (3D, see Gui::BaseView) or page object name. Old files without the block open as a single cell; old
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

## 7. M0 + M2 + M3(a) implementation notes (2026-08-25)

Landed as three commits: the container core, heterogeneous cells, and
default-on. `src/Gui/ViewArea.{h,cpp}`; commands in `CommandView.cpp`.

Deviations from and refinements of the plan:

- Commands are named for the user-visible direction, not the Qt
  orientation: `Std_ViewSplitRight` (side by side), `Std_ViewSplitDown`
  (stacked), `Std_ViewSplitClose`, plus `Std_ViewCellShowObject` -- the
  generic switch-the-editor operation. It resolves the selected object's
  view via `ViewProvider::getMDIView()` (materializing it with
  `vp->show()` when absent), so core Gui needs no TechDraw dependency;
  any view-bearing object type works.
- The activation virtual is `MDIView::activeSubView()`;
  `MainWindow::setActiveWindow`/`onWindowActivated` resolve through it
  and walk the widget chain up to the real `QMdiSubWindow`.
- `isBackgroundView()` moved up to `MDIView` unchanged;
  `View3DInventor`'s override defers to the enclosing container.
- TechDraw pages needed exactly two fixes: `ViewProviderPage` nulls its
  raw `QGVPage*` on view destruction (an embedded view dies without
  passing `removeMDIView`), and `showMaximized` is skipped for embedded
  views. The one-host-per-page rule holds: hosting a page steals its
  open MDI tab into the cell.

Lifecycle traps found by test, all fixed in the M3 commit:

- Adding the two virtuals to `MDIView` shifts every subclass vtable:
  workbench Gui libraries MUST be rebuilt (a partial build crashed at
  the Start page with a garbage slot call).
- A closed cell child is delete-on-close DEFERRED: it can outlive the
  document (nested event loops postpone deferred deletes), and the
  viewer's Coin sensors keep firing until the widget dies --
  `deferRedraw` read a freed `Gui::Document`.
  `View3DInventor::closeEvent` now decouples the viewer on any accepted
  close.
- `setCurrentViewMode(TopLevel)` reparents an embedded child away
  without killing it; the cell watches `ChildRemoved` and collapses.

Still open for M3(b): layout persistence -- the `<ViewArea>` block in
GuiDocument.xml recording the splitter tree (orientation/sizes, leaf ->
view id or page object name); without it a multi-cell layout reopens as
one container per saved camera, single-cell each.

## 8. M1 + M3(b) implementation notes (2026-08-25)

M1 (gestures): corner action zones exactly as planned in 5.4, with two
refinements -- the zones are invisible until hovered (Blender's are
too), and join-cancel is drag-back-inside rather than Esc (no keyboard
grab needed; returning to the press point also cancels, which required
running the disarm check BEFORE the split-start threshold). The border
menu on splitter handles carries close-left/right entries; maximize
(Std_ViewSplitMaximize) hides every sibling along the cell's path to
the root and restores the saved per-splitter states, and is unwound
automatically by split/close/replace. New splits always place the new
cell right/below regardless of drag direction (Blender places it at
the drag point; not worth the asymmetry yet).

M3(b) (persistence): `<ViewArea layout="..."/>` elements after the
`<View3D>` blocks, counted by a `viewareas` attribute on `<Camera>` so
old files read unchanged. Format:
`H{330,670|N:View1,V{500,500|N:View2,O:Page}}` -- permille sizes
(floored at 50 so a save while maximized cannot restore a cell
invisible), `N:<name>` = 3D view by its persistent name (Gui::BaseView,
adopted 2026-08-25 when the name feature landed; a bare name cannot be
the token because a leading `V` reads as a vertical splitter),
`L<i>` = 3D view by camera save order (the pre-name form, still read
for files saved in between and the fallback for a nameless view),
`O:<name>` = object view by document object name. The object token
comes from the view's Qt objectName (MDIViewPage sets it to the page
name) -- a provider-map scan is WRONG, a TechDraw template's provider
answers getMDIView() with its page's view and its show() cannot
recreate anything. Restore bare-creates the extra 3D views
(Document::createView3D) and applyViewAreaLayouts rebuilds containers,
materializing object views with vp->show(); a single-leaf layout whose
view already sits alone in a container is left in place. With the
progressive load's parked view providers the layouts apply in
finishDeferredRestore -- object views cannot exist before the drain.

Known rough edges, deliberate for now: cloneView transfers an active
editing view provider to the new cell on split; embedded children skip
the MainWindow windowStateChanged relay (spin-stop on tab switch);
closed donor containers linger hidden until the event loop runs their
deferred delete; maximize state is session-only; the per-cell content
menu (5.5) is still command+selection driven, no corner button UI yet.
M4 (wasm tier) untouched.

## 9. M4 design pass (2026-08-25)

The wasm tier per 5.7, refined against the code as it stands. The
desktop container (sec 5.1-5.6) is not involved: the browser viewer has
one canvas and one process-wide renderer, so the split is N sub-views
rendered by that one renderer, with DOM chrome owning the gestures.

### 9.1 Content model: cells select among RESIDENT stores

What a wasm cell can show is bounded by what the connection has: the
wire serves ONE group at a time (a scene payload flips s_pageMode off,
a page payload flips it on -- main.cpp applyScenePayload/
applyPagePayload), and per 5.7 the wire does not change in M4. Both
stores are resident at once, though: leaving page mode keeps the page
store's state, and the 3D snapshot stays while a page is served. So:

- A cell's content is `3d` or `page`, selecting between the RESIDENT
  scene and the RESIDENT page store.
- Live deltas flow only for the currently served group; a cell showing
  the other store shows its last-resident state. That is exactly the
  staleness the full-canvas page/3D toggle has today -- no regression,
  just visible side by side now.
- Serving two groups concurrently over one connection (true live mixed
  content) is a wire change: backlog, not M4.
- The current full-canvas page mode becomes a one-page-cell layout, as
  planned.

### 9.2 Renderer API (survey verdict + design)

Frame-path survey (2026-08-25) established:

- Everything camera/screen-space sized is per-BGFXView and tagged
  LifeSized in forEachHandle (scene MSAA, OIT, AO pyramid, volumetrics
  + history, media intervals, bloom, sceneCopy/present/accum/refl,
  debug + id readback), alongside the temporal caches (camFrameHash,
  accumFrames/accumProj, volAccumFrames, aoMapHash, refl/medium sample
  indices) -- all SINGLE-SLOT: alternating two cameras through them
  would never converge TAA/GTAO accumulation.
- Camera-INDEPENDENT and therefore shared: shadow map set +
  shadowMapHash (hashed from light matrices + casters only,
  BGFXFrame.cpp ~2377), bulb shadow atlas, the LifeProgram bucket
  (programs/uniforms/stand-in textures -- relink is seconds on some
  GL), the GPU geometry/texture caches (meshes/geometries/textures/
  textureArrays -- per-BGFXView today, so N full views would N-fold
  geometry VRAM: the killer argument against N BGFXViews), and the
  particle state (stepping an emitter twice a frame double-steps it).
- Every scene pass renders into view->bgfxFbo, not the backbuffer;
  only ViewPresent (standalone) touches the backbuffer, via a
  fullscreen triangle whose UV is 0..1 of the source regardless of
  view rect (vs_fc_comp.sc). So per-sub-view SIZED targets + a
  per-sub-view rect on the ONE setViewRect in configPresent
  (BGFXFrame.cpp ~3608) composes correctly with no shader change.
  Sub-rects inside one shared canvas-sized bgfxFbo would instead need
  UV remapping in every fullscreen pass: rejected.
- The overlay slots already run 9 (rect, camera) pairs into one
  framebuffer per frame, and BGFXDrawSurface already holds a second
  independent id block from the granule pool: both halves of the
  mechanism exist. Nothing yet runs the full pipeline twice inside
  one bgfx::frame() -- render() ends with the frame boundary, so it
  splits into a submit half and an endFrame half (present + frame +
  stats tail), the host calling submit N times, endFrame once.

Design:

- `Render::Renderer` grows

      struct SubViewFrame {
          int id;               // stable client token
          int x, y, width, height;   // backbuffer rect, device px
          const void *viewMatrix;
          const void *projMatrix;
      };
      virtual bool renderSubViews(const QColor &bg,
                                  const SubViewFrame *subs, int n);

  Default returns false (Coin/other backends). render() stays and
  becomes the n==1, full-canvas case internally.
- BGFXView keeps its member layout, but the per-sub-view group (the
  LifeSized handles, width/height/effW/effH/ssaoW/ssaoH, the id block
  fields viewId/viewSpan/viewLive/sinkView/idMap/passMark + sink
  targets, the temporal caches, warmup/targetsFailed) is declared
  through one X-macro list that also generates a SubViewBank struct
  and the stash/load swap. Sub-view i is rendered by loading bank i
  into the members, running the submit half, and stashing back --
  use sites stay untouched, and a new member added to the list is
  per-sub-view by construction. Bank 0 is the implicit full-canvas
  sub-view; desktop behavior is unchanged.
- One frame counter tick per wall-clock frame (endFrame), not per
  submit -- the mesh TTL (lastUsed + 2 < frame) keeps its meaning.
  collectMeshes runs once per frame.
- bgfx::reset keeps the CANVAS size; the per-bank width/height is the
  target size (the standaloneWidth==view->width equality splits into
  those two roles).
- Known approximations, deliberate: groundCam (shadow-ground sizing),
  relightForCamera and setAutoZoomScale follow the ACTIVE sub-view
  only -- they are feed-level, and per-sub-view values would thrash
  the shared shadowMapHash every pass. The GPU occlusion-query path
  carries cross-frame verdicts per camera and is disabled for n > 1;
  the software masked cull (the WebGL2 tier's path) is stateless per
  pass and unaffected.
- Id budget: a block per sub-view from the existing granule pool
  (~13 ids each); refusal falls back exactly as today.

### 9.3 wasm-side state (main.cpp)

Today's camera is file-scope globals (s_center/s_yaw/s_pitch/s_roll/
s_dist/s_panX/s_panY, s_userCam), the page view likewise (s_pageView,
s_pageUserView), and one render call per frame. M4 bundles them:

- `struct SubView { int id; content (3d|page); rect (device px, from
  the chrome); orbit camera fields; userCam; Page2D::View pageView;
  pageUserView; }` in a flat list owned by main.cpp, plus the active
  sub-view index. Single-entry list at startup == today's viewer.
- The frame loop renders the list: 3D cells through the new renderer
  entry (9.2), page cells through Page2D::render on their own view ids
  with the cell's rect (render() grows an x,y origin for its
  setViewRect; the Page2D store is retained/damage-based, so two draws
  a frame with setView() between them reuse the retained content).
- Input routing: pointer events hit-test the rect list (down/wheel/
  touch start pick the cell and focus it; drags stay with the cell
  that took the press, as the desktop gestures do). The cell's camera
  or pageView takes the interaction; pick/hover raycasts unproject
  with the cell's camera and rect. The NaviCube and ?cam= apply to the
  active cell.
- Fit-on-resize keeps its rule per cell: a cell whose user has not
  taken the camera re-fits when its rect changes.
- Per-sub-view state stays in the client (5.7): nothing joins the wire
  or the SceneDump snapshot.

### 9.4 DOM chrome (web/src)

A new `splitview.ts` module in the Solid chrome, mounted over the
canvas like the panels but full-viewport:

- The layout is a binary splitter tree in CSS-pixel space, same
  H{permille|...} shape as the desktop token (sec 8) so a layout could
  round-trip later; leaves are cells.
- Cell divs are `pointer-events: none` overlays (the canvas keeps its
  emscripten handlers) EXCEPT the interactive chrome: two 14px corner
  zones per cell (top-right, bottom-left, invisible until hovered,
  cross cursor -- ViewAreaZone parity) and the border handles.
- The gesture state machine mirrors ViewAreaZone exactly (ViewArea.cpp
  ~250-360): 12px manhattan threshold; inward drag splits on the
  dominant axis and the rest of the drag live-adjusts the fresh
  border; outward drag arms a join consuming the neighbor entered,
  with a dim overlay + arrow on the target; dragging back inside
  disarms (also right at the press point); release with an armed
  target closes it. Border drag resizes; the border carries a context
  menu (split/close entries) later -- menu parity is polish, gesture
  parity is M4.
- Rect push: on every layout change the chrome calls a new export
  `fcviewer_set_layout(json)` -- an array of {id, x, y, w, h, content}
  in CSS px (C++ folds in the dpr, as canvasPos does). The C++ side
  answers with nothing; it re-fits cameras for resized cells and drops
  state for vanished ids.
- The active cell gets a subtle border highlight; a small per-cell
  content chip (3D / Page) appears only when both stores are resident.
- The HUD card, menus and panels stay global (per-cell later if ever).


## 10. M4 implementation notes (2026-08-25)

Landed as four commits: the renderer support (M4a), the wasm-side
plumbing (M4b), the DOM chrome (M4c), and the smoke fixes. Verified in
real Chrome (native box, display :1) against a served demo-water scene
via a puppeteer-core drive: corner-drag split, independent per-cell
orbit (each cell with its own NaviCube and axis cross), border resize,
content chip to a page cell (backdrop-only when no page is served),
join drag with the dim + arrow overlay, and collapse back to the
single full-canvas view adopting a 3D cell's camera. Screenshots and
scripts in the session scratchpad (split-real.js, split-gesture.js);
serve rig: renderer-serve.sh with FC_BUILD=build/conda-relwithdebinfo-801
+ wasm-viewer.sh.

Deviations from the sec 9 design, and traps burned:

- Page2D::render never needed an origin overload: the CALLER owns the
  page view's setViewRect (renderPageFrame always did), so a page
  cell just sets its rect before render(vid, w, h).
- A fresh SubViewBank's zeroed msaaSamples reads as an MSAA change,
  and progChanged runs init(false) -- destroy + relink of the SHARED
  program set, once per fresh bank, all in one un-flushed frame. The
  handle pool (reclaimed only at frame boundaries) ran out, the
  bank's init failed, targetsFailed latched, and the cell rendered
  BLACK forever -- the standalone frame path lacked the desktop's
  "rebuild once, not every frame" lost-framebuffer clause. Three
  fixes: fresh banks inherit the live sample count in selectSubView,
  the standalone path gets the clause, and renderSubViews clears the
  latch once per wall frame (a multi-sub-view frame reaches
  bgfx::frame(), so a retry neither spins nor eats the pool).
- DOM gesture drags MUST listen on the window for the drag lifetime,
  never rely on element pointer capture: every tree change re-renders
  the chrome's cell divs, killing the element that captured the
  pointer -- the post-split live resize and the pointerup silently
  died, and the surviving stale drag object turned the next join
  gesture into a phantom resize.
- set_layout matches cells on id ALONE: matching on (id, content)
  made the 3D/Page chip flip look like a new cell and reset the
  hidden state set (a cell flipped to Page and back lost its camera).
  Clearing the layout adopts a 3D cell's camera (last-active
  preferred) for the single view; the chrome resets a lone surviving
  cell to 3D so its label matches the wire-driven single view.
- The first frame after a layout push can still bail once per new
  bank while the pool catches up (console: "sub-view N frame
  bailed"); it self-heals on the next frame via the retry.

Known limits, deliberate for M4: feed-level camera state (ground
sizing, relight, autozoom scale) follows the ACTIVE cell; the GPU
occlusion-query path is bypassed implicitly (the WebGL2 tier uses the
stateless software cull); bank 0's full-canvas targets stay allocated
while a layout is up (release is polish); layouts are session-only in
the browser (no persistence); the desktop tier does not use
renderSubViews (ViewArea composes whole widgets).


## 11. Polish pass: bail quirk, bank 0, browser persistence (2026-08-25)

Three of sec 10's known limits closed (the user's split-view polish
order). Verified together on real Chrome against a served demo-water
scene: gesture split, reload-restore, join, orbit, and 6 API
split/clear cycles -- zero "frame bailed" lines anywhere in the run.

### 11.1 The bail quirk: prepareSubViews warm-up + in-frame retry

The first frame after a layout push bailed once per new bank: a fresh
bank's init() allocates a full target set inside an un-flushed frame,
stacked on the resident banks' handles and on whatever destroys the
layout change queued (resized targets, dropped banks) -- destroys bgfx
reclaims only at frame boundaries -- so the create burst could find
the handle pool exhausted, latch targetsFailed, and render the cell
black until the next wall frame's retry.

Two mechanisms replace the visible retry:

- `Renderer::prepareSubViews(bg, subs, n)` (BGFXRenderer.cpp): for
  every unseen id, run a WARM submit -- the frame path with
  `subCtx.warm` set runs only through target allocation and returns
  before anything is drawn or ticked -- then cross a frame boundary,
  so each fresh bank allocates against a freshly reclaimed pool and
  its creates are realized before the next bank's burst. Idempotent
  and near-free once every bank is warm (map lookups), so
  renderLayoutFrame simply calls it every layout frame, BEFORE any
  page cell queues a draw -- prepareSubViews crosses frame
  boundaries, and a boundary after a queued page draw would commit it
  early and drop that rect from the frame's composite. Sitting in the
  frame loop rather than fcviewer_set_layout also covers a layout
  restored before the renderer exists (11.3).
- The standalone frame path retries in-frame when the build fails
  with nothing queued (`!subCtx.active || subCtx.warm`): pump
  bgfx::frame() to reclaim the destroy backlog, clear the latch,
  init once more. This is what heals the return to single view --
  the layout clear drops every cell bank and then plain-renders a
  bank released by 11.2, all in one frame. Mid-sequence submits keep
  the bail-and-heal-next-frame path: a boundary there would commit
  queued sibling/page passes early (same composite argument).

The warm flag is deliberate about what it skips: no frame-counter
tick and no collectMeshes (a restore pushing several fresh banks
would otherwise age the mesh TTL several frames in one RAF and sweep
meshes the siblings still draw), no particle stepping, no draws.

### 11.2 Bank 0 released under a layout

Chrome-issued cell ids are >= 1, so with any layout up the implicit
full-canvas bank is dead weight -- a whole MSAA scene + AO + OIT +
present set at canvas size. renderSubViews and prepareSubViews both
run `releaseBankZero`: when no sub is id 0 and bank 0 still holds a
live framebuffer, it gets the dropSubView dance (destroyTargets,
releaseIds, erase), and the return to the single view rebuilds
through the ordinary fresh-bank path -- protected by the in-frame
retry above. The map-entry erase matters: a stashed bank full of
destroyed handles would otherwise resume with stale temporal state.
All-page layouts keep bank 0 allocated (renderSubViews never runs);
rare enough to leave.

### 11.3 Browser layout persistence (localStorage)

Decision: localStorage on the chrome side, keyed by the served
document -- `fc.split.<docName>`, falling back to
`fc.split.@<scene-url>` when no document name is known. A layout is
chrome state, not document content (different devices legitimately
want different splits of the same document -- the desktop analog
persists per install's GuiDocument.xml), and a server-side store via
SceneServer grants would be the wire change M4 deliberately avoided.
Cameras are NOT persisted: restored cells clone the live camera /
fit, like any fresh cell.

Mechanics (web/src/splitview.tsx):

- Saved on every tree change through the changed() funnel, debounced
  300ms: `{v:1, tree}` with cells `{id,page}` and splits
  `{dir,ratio,a,b}`. A tree collapsed to one cell REMOVES the entry.
- Restored when the viewer side is up -- a 250ms poll for
  window.fcviewerSetLayout, since the chrome usually mounts before
  the WASM module registers its exports and a push before that is
  silently dropped -- and re-keyed on 'fc:docs' (initial identity)
  and 'fc:docswitch' (a custom event main.tsx dispatches from
  switchDoc, because no docs push follows a switch). A key change
  with nothing stored collapses to the single view.
- Restored cells are renumbered from nextId: stored ids are not
  trusted, so a corrupt store cannot produce duplicate ids (set_layout
  matches cells by id alone, sec 10).

Known cosmetic, pre-existing: one "pass map: N draw(s) went to the
discard view from pass 6" line per fresh bank's first frame (the
report is once per bank by construction -- sinkReported is a bank
field); it predates this pass and costs one pass's pixels for one
frame.


## 12. Desktop polish pass (2026-08-25)

The split-views-built rough-edge list, closed. Verified by an Xvfb
smoke (split_polish_smoke.py, session scratchpad) plus the M0-M3
smokes re-run as regression.

- **Per-cell menu button** (5.4/5.5): a 16px grip in every cell's
  top-left corner (objectName `ViewAreaMenuButton` -- the class lives
  in ViewArea.cpp without Q_OBJECT, so the name is what tests and
  stylesheets find it by), subtle until hovered. Its menu is the
  content selector -- "3D view" plus one entry per object-provided
  view (materialized views anywhere, TechDraw pages by type name, no
  Gui->TechDraw dependency) -- then Split horizontal/vertical,
  Maximize/Restore, Close. The 3D entry clones a sibling 3D cell
  first (its camera is the area's context), any document 3D view
  second, bare createView3D last.
- **Split during edit**: Document::cloneView grew a transferEdit
  parameter (default true keeps the view-mode-workaround and
  settings-dialog callers, which replace the original view).
  ViewArea::cloneChildFor passes false: the editing view provider
  stays in the view the user is editing in, instead of the fresh
  cell stealing the dragger mid-edit.
- **Window-state relay**: hostView/releaseView now make/break the
  MainWindow::windowStateChanged connection addWindow/removeWindow
  manage for top-level views (MDIView befriends ViewAreaCell for
  it), so an embedded 3D view hears another tab maximize over it and
  arms its stop-spin timer.
- **Dead containers vanish promptly**: ViewArea::deleteSelf detaches
  the QMdiSubWindow shell from the MDI area after the close --
  MDIView::deleteSelf only close()s it, which hides but keeps its
  TAB until the deferred delete runs, indefinitely in a nested event
  loop. The restore path's empty donors (materialize-then-rebuild)
  measurably stopped lingering: the M3b smoke's reopen went from 2
  areas / 4 tabs to 1 / 2.
- **Maximize persists**: the `<ViewArea>` element carries a
  `maximized="<leaf token>"` attribute; restore re-maximizes via
  ViewArea::setPendingMaximize (deferred one tick past real
  geometry, see below). While maximized, layoutString saves the
  UNDERLYING proportions -- toggleMaximizeCell records each
  splitter's plain sizes next to its opaque saveState blob, and
  preMaximizeSizes feeds them to the serializer.

The proportions hunt uncovered a pre-existing bug worth naming:
**applyLayout flattened every restored layout to near-equal shares.**
Three stacked causes, all fixed in ViewArea.cpp:

- The root-adoption step read `sp->sizes()` off the parser's
  never-shown splitter, which does not answer with what setSizes
  stored. It now reads the parser's intent (`ViewAreaSplitter::
  initialSizes`).
- setSizes with a sum below the splitter's extent hands the missing
  space out EQUALLY, skewing every ratio toward even (a permille
  list on a 1500px splitter restored 0.70 as 0.64). All re-apply
  paths go through `applySizesScaled`, which scales to the live
  total first.
- A pre-show setSizes is mangled at realization, so
  ViewAreaSplitter::resizeEvent re-applies initialSizes at the first
  VISIBLE resize with a real extent -- hidden default-size resizes
  must not consume it, and the maximize capture prefers a
  still-pending initialSizes over live sizes for the same reason.

Smoke round trip: a 70/30 split, maximized, saved, reopened (comes
back maximized), un-maximized -- restores 0.70 exactly (was 0.50).


## 13. Desktop tier onto renderSubViews: design pass (2026-08-25)

The last M4 known limit: the desktop container composes whole child
widgets -- every 3D cell is a View3DInventor whose QuarterWidget owns
a GL surface, and every such widget gets its OWN BGFXView in
_BGFXLib.views (keyed by QOpenGLWidget). N cells therefore carry N
copies of the GPU scene caches (the killer argument sec 9.2 already
named), N context dances and N blits per wall frame. The order: one
canvas, N cameras+rects -- the wasm model.

### 13.1 Ground truth (desktop composite, surveyed)

- Desktop bgfx runs on its OWN QOpenGLContext, built against Qt's
  global share context (deviceSharesQtGL). A frame renders into
  view->bgfxFbo on that context, then BGFXView::blit transfers color
  AND depth into whatever framebuffer the caller has bound -- the
  widget's own for on-screen (View3DInventorViewer::renderScene
  restores hostFbo before the blit), a capture target for
  renderOffscreen.
- Coin then composites everything the backend does not claim ON TOP,
  inside the same widget context, depth-tested against the blitted
  backend depth. In backend mode that residue is small (the backend
  draws claimed scene, background, captured overlays) but includes
  the EDITING draggers and any uncached custom nodes.
- The blit FBO cache (fbo/fboDepth/hasFBO/blitColorId/
  blitSourceEncoded) wraps ONE view's textures and is NOT in
  FC_SUBVIEW_FIELDS -- per-bank textures need it banked on desktop.
- The bank machinery itself (SubViewBank, selectSubView, subCtx) is
  compiled in both builds already; only the frame-path entry points
  (selectSubView at frame start, sub-view sizing, the present rect)
  are standalone-gated today.
- Each viewer FEEDS its own renderer instance from its own
  selectionRoot capture; one document shown in N cells captures N
  near-identical scenes.

### 13.2 Decision

One canvas per container, hidden-but-sized children as the
compatibility spine:

- The container hosts ONE QOpenGLWidget canvas; it is the single
  _BGFXLib.views key for the whole area, and cells are banks of its
  one BGFXView -- the same architecture, and largely the same code,
  as the wasm tier. ONE renderer instance, owned by the canvas
  hosting, fed by the ACTIVE 3D cell's viewer (cells share the
  resident scene, wasm sec 9.1 parity).
- Child View3DInventors STAY -- hidden but resized to their cell
  rects. Everything that made M0 cheap keeps working unchanged:
  activation resolution, the 53 activeWindow() call sites, camera
  persistence, message routing, AND all input math -- events
  forwarded to a hidden widget of the right size need no coordinate
  model at all beyond the cell offset.
- Per submit, the desktop frame runs as today (its own bgfx::frame
  per submit is FINE on desktop -- there is no backbuffer swap, the
  blit is the composition, so the wasm's one-wall-frame batching is
  unnecessary); the blit gains a destination rect.
- Coin residue runs per cell after the backend blit: viewport +
  scissor to the cell rect, the child viewer's render action applied
  in the canvas context. This is the deep risk (13.4).

What this deliberately narrows, exactly as the wasm tier did: cells
of one canvas show the SAME resident scene (per-cell display override
modes need per-cell feeds and fall back to widget composition; a
later per-sub-view pass filter -- faces vs lines per bank -- can
bring Blender-style per-viewport shading back cheaply). Page cells
and plain-GL mode keep widget composition.

### 13.3 Milestones

- **D1 -- renderer: desktop renderSubViews** (this session). Un-gate
  the bank machinery for the desktop frame path: selectSubView at
  frame entry, subCtx-driven target sizing (subCtx.w/h beats
  viewWidth/viewHeight, the captureWidth pattern), blit to a
  destination rect (GL y-flip against the canvas height), the blit
  FBO cache banked via a desktop-only FC_SUBVIEW_FIELDS extension,
  releaseBankZero + prepareSubViews enabled for desktop (each warm
  submit is an ordinary desktop frame; the in-frame retry gate stays
  as is because every desktop submit crosses its own frame
  boundary). Occlusion queries bypass for n > 1 as on wasm.
- **D2 -- ViewArea unified canvas**, pref-gated
  (View/UnifiedCanvas, default OFF until proven): a canvas widget
  under the splitter tree's 3D cells; children hidden-but-sized;
  canvas paint = renderSubViews over the 3D cell list + per-cell
  Coin residue; input forwarding by cell rect (sendEvent to the
  hidden child). Cells with non-3D or per-cell-override content stay
  widget-composed (mixed mode: the canvas covers the 3D subset).
- **D3 -- per-cell chrome parity**: NaviCube/axis cross per bank
  (per-sub-view overlay feeds), active highlight on the canvas,
  gesture overlays above it.
- **D4 -- per-sub-view display styles**: a pass filter per bank
  (shaded / wireframe / hidden-line per cell) restoring per-viewport
  shading without per-cell feeds.

### 13.4 Named risks (for D2+)

- **Coin cache contexts**: a child's GL caches are keyed to its own
  cache-context id; traversing into the canvas context needs the
  render action's cache context aligned to the canvas (same share
  group makes the objects valid; the id keys the caches). Quarter
  assigns ids per widget -- the residue pass must run the child's
  scene with the CANVAS's id or Coin rebuilds/mixes caches.
- **Widget-over-GL stacking**: zones, menu button and join overlays
  must stack above the canvas (Qt composites plain children over a
  QOpenGLWidget sibling, but ordering quirks are real).
- **Editing**: the dragger residue must land in the right cell;
  setEdit stays with the child viewer, which no longer paints.
- **Screenshot/print/offscreen**: per-child paths key off the child
  widget; they must route to the canvas renderer with the child's
  camera (renderOffscreen already takes explicit matrices+size).
- **isBackgroundView / target release**: the canvas answers for all
  its cells; per-cell release stops making sense (drop whole-canvas).

### 13.5 D1 implementation notes (2026-08-25)

The renderer half is BUILT (both tiers compile; the desktop shared
path is regression-clean -- the polish smoke reruns byte-identical):

- BGFXView::blit takes a destination rect (dstX, dstY top-left widget
  coords + dstH for the GL y-flip; zeros keep the full-surface
  transfer). Color AND depth both land at the rect, so the Coin
  residue pass composites depth-tested per cell.
- The desktop blit FBO cache (fbo/fboDepth/hasFBO/blitColorId/
  blitSourceEncoded) is banked via FC_SUBVIEW_FIELDS_HOST, a
  desktop-only extension of the bank field list -- it wraps one
  bank's textures and must swap with them.
- The frame-entry selectSubView and the whole bank machinery now run
  on the desktop path too (they were standalone-gated); a plain
  render() stays bank 0 and swaps nothing.
- Desktop renderSubViews: one ORDINARY desktop frame per submit --
  sized through the captureWidth override exactly as renderOffscreen,
  blitted to the sub rect of whatever framebuffer the caller bound.
  No wall-frame batching: there is no backbuffer swap on this path,
  the blit is the composition, and per-submit frame boundaries mean
  fresh banks allocate against a drained pool -- so desktop
  prepareSubViews is releaseBankZero only, no warm pass.
- The GPU occlusion-query cull is bypassed for sub-view frames
  (per-camera verdicts landing frames later must not cross cells);
  the software masked cull keeps working per pass.

NOT yet consumed: D2 (the ViewArea canvas) is next-session work.
Survey nuggets for it, from this pass:

- The scene FEED happens during Coin traversal (selectionRoot's
  external-renderer capture inside the widget's paint) -- a canvas
  mode must run the child's traversal in the canvas context or
  nothing feeds; there is no paint-free feed path on the desktop.
- A hidden QWidget's update() is a no-op, so the child viewers'
  scheduleRedraw must be redirected to the canvas -- a small
  QuarterWidget hook (redraw() indirection) in our fork.
- Cells must become background-less widgets so the canvas sibling
  UNDER the splitter tree shows through (Qt composites siblings by
  stacking order; the join overlay already relies on this class of
  behavior).

## 14. D2 implementation notes: the ViewArea unified canvas (2026-08-25)

D2 of the sec 13.3 ladder is BUILT and verified on the real GPU (RTX
3060, xvfb + VirtualGL). Pref-gated `View/UnifiedCanvas`, default OFF,
applied live in both directions (`ViewParams::onUnifiedCanvasChanged`
re-syncs every open container).

### 14.1 What it is

`Gui::ViewAreaCanvas` (src/Gui/ViewAreaCanvas.{h,cpp}) is a
QOpenGLWidget child of the ViewArea, sized to the root splitter's rect
and lowered under it. It owns ONE backend instance
(`RendererFactory::create(type, canvas)`), and the container's 3D cells
become banks of it. A canvas frame is:

1. one `SubViewFrame` per claimed cell -- the cell rect in canvas
   DEVICE pixels, top-left origin, plus that cell's own camera read
   against the CELL's aspect ratio;
2. `prepareSubViews` then `renderSubViews` (D1's desktop path: one
   ordinary frame per submit, blitted colour+depth to the sub rect);
3. the FEEDING cell's Coin residue through
   `View3DInventorViewer::renderCanvasResidue`, then a follow-up frame
   if `needsRedraw()` -- the same one-frame-lag rule renderScene()
   already lives by (the feed happens during the traversal, which runs
   after the backend pass).

The child View3DInventors stay: out of the cell's layout, resized to the
cell rect by hand, and hidden. Not reparented -- `ViewAreaCell::childEvent`
reads a reparent as the view being torn away and collapses the tile.

### 14.2 The pieces it needed

- **`View3DInventorViewer::adoptRenderer(shared, feed)`**. `_pimpl->renderer`
  became a `shared_ptr`, so the canvas's instance can be handed to every
  cell; `feed` selects the ONE cell whose render-cache manager states the
  scene to it. Two managers pushing `setScene()` at one backend would
  overwrite each other, so the rest supply only a camera. The feed
  follows the ACTIVE cell (`ViewArea::setActiveCell` re-syncs) so
  draggers land where the user is working; moving it costs a
  re-translation of the caches the new feeder already holds
  (`SoFCRenderer::feedExternal` -- no traversal).
- **`QuarterWidget::setRedrawRedirect`**. A hidden widget's `update()`
  is a no-op, so every redraw Coin schedules for a child would be
  dropped; the redirect sends it to the canvas.
- **`renderCanvasResidue(origin, size, backendDrawn)`**. Sets the render
  manager's viewport region to the cell rect ORIGIN INCLUDED (Coin draws
  where the region says), scissors the whole traversal to it -- both of
  renderScene's clears are framebuffer-wide -- and runs renderScene with
  `canvasResidue` set, which skips the backend frame the canvas already
  drew.
- **Input forwarding by cell rect**. The canvas installs an event filter
  on each claimed cell and re-sends mouse/wheel/key events to the hidden
  child's GL widget at the coordinates it would have seen. Presses also
  set the active cell directly: a hidden widget cannot take focus, so
  activation can no longer ride `ViewArea::onFocusChanged`.
- **Cells go background-less** (`WA_NoSystemBackground`) so the canvas
  sibling under the splitter tree shows through.

### 14.3 Traps hit

- **The forwarded event climbs straight back in.** The hidden child is
  still a CHILD of the cell, so anything it leaves unaccepted propagates
  up into the cell -- and into the filter that forwarded it, which
  forwards it again. Two clicks were enough to blow the stack (SIGSEGV).
  Fixed with a re-entrancy flag (`_forwarding`); while set, the filter
  lets the event climb past.
- **A sub-view blit rect is in the destination framebuffer's own
  pixels.** D1 passed `widget->height()` (logical) as the y-flip
  reference; the destination is the widget's FBO, which is DPR-scaled.
  Now `height() * devicePixelRatioF()`, and the canvas states its rects
  in device pixels.
- **A claim is against a cell AND its child.** `setCellView` swaps the
  content under a claimed cell; without comparing the child the newcomer
  would never be hidden or adopted. `ViewAreaCell::releaseView` also
  releases the claim before the view leaves.
- **Teardown must not restore backends.** `releaseAll(false)` from the
  ViewArea destructor detaches without giving each viewer a backend of
  its own -- otherwise two are created only to be destroyed with the
  widgets a moment later.
- **A maximized layout hides every other tile**, so `claimable()` skips
  invisible cells and a single visible cell drops the canvas entirely
  (nothing to share). The same rule keeps an unsplit view on the plain
  path, which is why the regression smokes are byte-identical.

### 14.4 Verified

- Two cells, one canvas: both render their own camera, and the picture
  is PIXEL-IDENTICAL to widget composition except the active-cell
  border (see 14.5). 0 "frame bailed" lines.
- Three cells: all three claimed, all three render.
- Mixed: a TechDraw page cell stays widget-composed and visible while
  the two 3D cells stay canvas-drawn -- the sec 13.2 narrowing, working.
- Activation follows a click into a cell (each cell's camera stamped and
  read back through the active view).
- Pref off at runtime: canvas gone, children visible, same picture.
- Regression with the pref off (the default): the polish, default,
  layout, gesture, M0 and page smokes all reproduce their baselines
  exactly.

### 14.5 Known gaps, all D3/D4 work

- **The active-cell highlight border is invisible** under the canvas:
  the cell paints it into the backing store and the GL sibling covers
  it. This is 13.4's widget-over-GL stacking risk, and 13.3's D3 already
  names "active highlight on the canvas".
- **Only the feeding cell composites Coin residue.** The others draw the
  resident scene through their camera and nothing else -- correct today
  because the feed follows activation, but per-cell chrome (NaviCube,
  axis cross) is D3.
- **Overlay captures outlive their feed.** A cell that stopped feeding
  leaves its overlay entries resident in the backend, and they are drawn
  in EVERY sub-view. They currently overlap exactly (same anchor), so it
  looks right; per-sub-view overlay feeds are D3.
- **Per-cell display modes** still fall back to widget composition
  (D4's pass filter).
- Cells showing a DIFFERENT document are not claimed: one canvas draws
  one resident scene.

## 15. Direction change: D4 becomes the Coin display-mode removal (2026-08-26)

User order, 2026-08-26: **remove the legacy Tessellation display mode
and the Coin-based display modes like it, and use the backend to achieve
the same effect.** Recorded in full as
`docs/CoinRetirement.md` **Stage 5** (taxonomy, cost, replacement
sketch, order); this section is only what it changes for this ladder.

**D4 is that stage's first consumer, and its mechanism is the same
one.** 13.2 narrowed the canvas by saying per-cell display override
modes "need per-cell feeds and fall back to widget composition", with a
later per-sub-view pass filter as the way back. Stage 5 says the pass
filter is not a split-view special case at all -- it is how every
display style should work, because a style that lives in Coin traversal
state (`SoFCDisplayModeElement`, the `SoFCSwitch` named override,
`SoRenderManager::HIDDEN_LINE`) is per-VIEWER state, and the canvas has
one viewer feeding N banks. Once a style is a backend draw-time
parameter, per-cell styles cost a per-bank field and nothing else, and
the widget-composition fallback for override modes can go.

Consequences for the ladder:

- **D3 is unchanged and still next** -- per-cell chrome parity
  (NaviCube/axis cross per bank, active highlight on the canvas, gesture
  overlays above it). It does not touch display styles.
- **D4 is no longer "add a pass filter for split views".** It is the
  split-view half of Stage 5, and it inherits Stage 5's step 1: survey
  which ViewProviders put genuinely different GEOMETRY under each
  display-mode child of their `SoFCSwitch`. Where that is true the style
  cannot be reproduced from one capture, and those cells keep the
  widget-composition fallback; where it is false (the expected common
  case) the cell becomes a bank field.
- **Stage 5 step 2 (Tessellation) is independent of split views** and
  can land first: the backend already implements it
  (`BGFXView::submitTessellation`), and `applyOverrideMode()` already
  refuses Coin's `HIDDEN_LINE` whenever a renderer exists, so the Coin
  half is dead code on the default configuration.

## 16. D3 implementation notes: per-cell chrome (2026-08-26)

### 16.1 Widget chrome already stacks above the canvas -- measured

13.4 named "widget-over-GL stacking" as a risk for D2+, and 14.5 read
the invisible active-cell border as evidence of it. **It is not a
risk: plain child widgets of a cell paint above the canvas.**

Measured (`d3b.py`, RTX 3060 under Xvfb): the 16x16
`ViewAreaMenuButton` at each cell's top-left draws 221 and 218 ink
pixels over its local background with the canvas ON, and 218/218 with
it OFF. Same picture either way -- Qt composites the plain child over
the QOpenGLWidget sibling exactly as it does over the child
View3DInventor.

So the zones, the menu button and the join overlays need nothing, and
per-cell chrome that CAN be a widget should stay one. Only content the
backend owns (the NaviCube, the axis cross) needs the per-bank work in
16.2.

### 16.2 The active-cell border was never visible on EITHER path

Chasing the border turned up a defect older than the canvas.
`ViewAreaCell::paintEvent` drew it on the cell itself -- but the cell's
layout has zero contents margins and the child view fills it, and the
child is painted over its parent. **The border has always been drawn
underneath the child view.** The canvas did not hide it; the canvas
just made it noticeable, because 14.5 went looking for it.

The fix follows 16.1: `ViewAreaHighlight`, a raised child widget of the
cell, drawn over the child view and over the canvas alike -- one
implementation for both paths instead of a GL one for the canvas and a
Qt one for widget composition.

- **Masked to the ring.** The widget is sized to the whole cell, then
  `setMask`ed to the 1px border. It therefore overlaps the GL surface
  by only the pixels it draws, rather than laying a full-cell
  translucent widget over it. The mask is in local coordinates, so
  `refit()` re-cuts it on every resize.
- **`WA_TransparentForMouseEvents`**, or a full-cell child would eat
  every press before the cell's filter forwarded it.
- **Raised UNDER the zones and the menu button** (`hostView` raises it
  first), so a corner grip stays whole where the ring crosses it.
- **Activation repaints the widget, not the cell.**
  `ViewArea::setActiveCell` called `old->update()` / `cell->update()`,
  which now repaints everything except the thing that changed;
  `updateHighlight()` replaces both.
- **A cellCount change rides the resize.** The border is suppressed
  below two cells; a join resizes the survivor and a split resizes
  both, so `resizeEvent` -> `refit()` + `update()` covers the
  transition without a separate hook.

Verified (`d3a.py`, real GPU): sampling 12 points around each cell's
border ring against the palette Highlight colour the cell itself
reads -- active cell 12/12, inactive 0/12, with the canvas ON; the
highlight follows a click to the other cell (0/12 and 12/12 after);
and with the canvas OFF the same samples read 12/12 and 0/12, so
`paths-agree-active` and `paths-agree-inactive` are both true. Before
this change the canvas-OFF leg read 0/12 on the ACTIVE cell -- the
defect above, caught by the smoke rather than assumed.

Regression: `canvas.py` and `mixed.py` (the D2 smokes) both reproduce
their sec 14 baselines line for line.

### 16.3 Per-sub-view overlay feeds

The last D3 item, and the one that needed work in three layers.

**The problem.** Every cell of a canvas is a bank of ONE backend, and
that backend's overlay map is keyed by **producer id alone**. Only the
FEEDING cell ran its overlay captures (they happen inside
`renderScene`, and no other cell paints), and what it fed was drawn in
every sub-view. So each cell drew the FEEDER's NaviCube, turned by the
FEEDER's camera -- and 14.5's "overlay captures outlive their feed" was
the same defect seen from the other side.

**The fix, in three parts:**

- **A sub-view scope on the anchor.** `Render::OverlayAnchor` gains
  `int subView` -- 0 meaning every sub-view, which is one viewer's
  chrome in all of them and what a plain `render()` draws. Putting it on
  the ANCHOR rather than in the `setOverlay` signature means it rides
  through `SoFCRenderer` and `SceneDump` (v69) with no API change, and
  `setExternalOverlay`'s early-out re-keys correctly when a cell's id
  changes, because the anchor is part of what it compares.
- **Ids offset per cell.** `OverlayIdStride` (16, > the largest
  `OverlayId`): a cell feeds under `subView * 16 + base`, so two cells'
  NaviCubes are two map entries instead of one that each overwrites in
  turn. Every feed goes through one `feedOverlay` lambda in
  `updateOverlayCaptures`, so the scoping cannot be forgotten at one of
  the eight sites.
- **The canvas drives every claimed cell.**
  `View3DInventorViewer::updateCanvasOverlays()` runs the capture
  traversals without painting; `ViewAreaCanvas::paintGL` calls it for
  each cell it is about to draw, before the frame, so all feeds are
  resident by the time any sub-view renders. `renderScene` skips its own
  call while `canvasResidue` is set -- the canvas has already driven it.

Frame side: the two overlay loops (`configOverlay`, which sets a slot's
viewport and camera, and the submission loop) now walk ONE resolved
`frameOverlays` list. **They disagreed before**: the submission loop
skipped viewport chrome on a chromeless dump and the config loop did
not, so every slot past the first skipped one was configured from the
wrong anchor. Resolving both filters -- sub-view and chrome -- in one
place fixes that as a side effect of needing it for this.

**Two defects this turned up, both invisible until the id decided
content.**

- !! **A re-entrant `sync()` double-claimed a cell.** `claim()` records
  its entry LAST, after pulling the child out of the layout, hiding it,
  installing the event filter and adopting the backend -- any of which
  can deliver an event that reaches `ViewArea::setActiveCell`, which
  calls `sync()`. The nested run saw the cell as unclaimed and claimed
  it again under a second id. The canvas then drew a phantom third
  sub-view, and since a viewer carries only the id of its LAST
  adoption, that cell fed its chrome under one id while its phantom
  bank rendered under the other -- so it drew NO chrome at all. Fixed
  with a `_syncing` guard that replays one nested call after the outer
  one finishes (dropping it would lose a real layout change). Same
  class as 14's forwarded-event recursion.
- **The global `ShowNaviCube` preference does not reach a cloned view.**
  Enabling it after a split left one cell's viewer with
  `naviCubeEnabled` false, so that cell had no cube to feed. Enabling it
  BEFORE the split -- so the clone inherits it at construction --
  reaches every viewer. Not fixed here (it is a `View3DSettings`
  question, not a canvas one) and recorded so the next reader does not
  spend the time again. The smoke sets the preference up front.

**Verified, real GPU** (`d3d.py`): two cells, both showing their own
NaviCube and corner axis cross. With the same camera in both, their
chrome corners agree (216/11400 samples differ, the scene behind the
chrome). Turn ONLY the non-feeding cell to Top: its chrome follows its
own camera (964/11400 differ) while the feeding cell's does not move at
all (0/11400), and the two cells' chrome now disagrees (1180/11400). The
picture reads FRONT in one cube and TOP in the other. Regression:
`canvas.py`, `mixed.py` and `d3a.py` all reproduce their baselines.

! **Choosing the subject matters here.** The corner axis cross alone
cannot prove this: its anchor is `orientFromScene`, so the BACKEND turns
it by the frame's view matrix and a single shared feed would still look
per-cell. The NaviCube is the discriminator -- its orientation is baked
into the captured graph by the viewer that owns it. (And
`View3DInventorViewer::setAxisCross`, the Python API, is a different
thing again: it puts an `SoAxisCrossKit` in the SCENE at the origin, not
the corner chrome that `axiscrossEnabled` controls.)
## 17. D4 implementation notes: a display style per cell (2026-08-26)

D4 is the split-view half of `docs/CoinRetirement.md` stage 5. It went
through one wrong design before this one, and the correction is the
point of the section, so it is recorded rather than tidied away.

**The rule, stated by the user:** `As Is` respects each object's own
`DisplayMode`; any other style **overrides** it, for every object. That
is what Coin does, and it was not to change.

**Why a canvas makes that hard.** A style is an override applied by the
Coin traversal, and a unified canvas has exactly ONE feeder -- "two
feeds at one backend overwrite each other's scene" (`adoptRenderer`).
So one traversal produces the capture that every cell draws, and
whatever style that traversal ran under is baked into it. That is why a
non-feeding cell's style change showed nothing before D4.

**The design that was wrong.** First attempt made a Class-A style a
bucket filter at submit: the traversal captured each object's own mode
and each cell dropped the primitive buckets its style does not draw
(`Render::DrawStyleMask` on `SubViewFrame`). It passed its own smoke,
because everything in the test was in the default `Flat Lines`. But a
filter is not an override. Measured, with a box whose own `DisplayMode`
is `Wireframe`, in a cell asking for `Shaded`:

| | single view (Coin) | canvas cell (filter) |
| --- | --- | --- |
| ink | **24434** | **0** |

Coin traverses the object's `Shaded` child and shows faces; the filter
has no faces to keep and shows nothing. A filter can only ever remove.

**The design that is right, and it was already in the tree.** A style
that cannot be served by the shared capture is the same kind of
conflict as a cell showing another document, which `claimable()` has
always handled by letting that cell keep its own widget composition. So
the odd cell simply stops being claimable, leaves the canvas, and
renders itself -- and its own Coin traversal then applies its style,
which makes the override an override *by construction*. `syncOnce()`
already rebuilds the claim set from scratch every sync, so the cell
comes back the moment it can be served again; nothing new was needed
for either direction.

**The filter is kept, but only where it is provably identical.** Two
cells in different styles are not a conflict by themselves. The
question is per OBJECT: filtering a capture taken in the objects' own
modes equals the override exactly when every visible object's own mode
already carries the buckets the style asks for. `styleConflicts()` is
that test, and on a document whose objects are all in the default mode
it is always false -- so the common case keeps one capture, one scene,
and a style change that costs no re-traversal at all.

`ViewAreaCanvas::resolveDisplayStyles()` picks between three states
once per sync:

- **one style** -- every claimable cell agrees. The traversal applies
  it, exactly as a plain view does and exactly as this did before
  per-cell styles existed. No filtering.
- **filtering** -- the styles differ and `styleConflicts()` says
  nothing in the document can tell a filter from the override. Every
  cell stays claimed; the traversal captures own modes
  (`View3DInventorViewer::setCanvasStyleFiltered`), each cell filters.
- **split off** -- the styles differ and something does conflict. The
  style the most cells share keeps the canvas (the active cell breaks a
  tie), and the rest are released to their own backends.

**Debounced.** The conflict test walks every visible view provider, and
its two inputs -- `signalViewModeChanged`, and `signalChangedObject`
filtered to `DisplayMode` / `Visibility` -- arrive in bursts: an
import, a delete of a hundred objects, a visibility sweep. A restarting
150 ms `QTimer` collapses a burst into one walk, and also stops a cell
being handed its own backend and having it taken away again midway
through one.

! **An unrecognized display mode counts as a conflict.** Mesh's
`Point`, FEM's `Faces & Wireframe` and anything a Python ViewProvider
registers have no bucket mask, so the test cannot weigh them and
returns true. Being wrong that way costs a cell its share of the
canvas; being wrong the other way draws the document incorrectly.

Verified (RTX 3060, xvfb + vglrun egl0), box with own `DisplayMode` =
`Wireframe`, two cells: both `As Is` -> canvas on, ink `[1063, 1063]`;
cell 1 -> `Shaded` -> canvas off, ink `[1063, 24460]` dark `[494, 0]`,
i.e. that cell shows the faces the single view shows (24434); own mode
-> `Flat Lines` with both styles untouched -> canvas back on by itself,
ink `[24846, 24459]` dark `[391, 0]`, the styled cell now served by
filtering; cell 1 -> `As Is` -> one style again, ink `[24846, 24846]`.

## 18. The visible chrome points at the corner zones (2026-08-28)

The corner action zones paint nothing until the cursor is inside them
(sec 5.4, Blender's behavior). That is fine once you know they exist and
useless before: a 14px transparent corner advertises nothing, and the
split/join gesture is the main thing a cell can do.

The menu button is the one piece of cell chrome that IS visible unprompted
(a grip in the top-left corner, sec 12). So hovering it now reveals both
zones as well -- top-right and bottom-left light up together with the
button, and go dark again when the cursor leaves. Learning one gesture
surfaces the other two corners for free, and nothing is added to the
resting frame.

- `ViewAreaZone::setHint(bool)` is the second reason to paint. `paintEvent`
  draws on `_hover || _hint`, and a hinted-only zone drops the grip strokes
  to alpha 130, so a zone the cursor is actually on still reads as the
  live one.
- `ViewAreaCell::showZoneHint(bool)` forwards to both zones; the menu
  button's `enterEvent`/`leaveEvent` call it. The cell owns both zones,
  which is why the pairing lives there and not in the zone.

A splitter handle is the second such trigger, and the better one: a user
who has found the border is already thinking about the layout, and the
corner zones are how that border is made and removed.
`ViewAreaSplitterHandle::enterEvent` calls `ViewArea::showZoneHintAt`,
which lights the cells that handle borders and clears every other cell.

- Bordering is decided geometrically, not from the splitter tree: a
  handle's own children are only two widgets, and either may be a nested
  splitter whose leaves partly touch the handle and partly do not.
- Both halves of the test are needed. A cell must overlap the handle
  ALONG its length and have an edge ACROSS it that is the handle's own
  edge (2px of slack for the frame). Dropping the second lights the whole
  row; dropping the first lights the whole subtree.
- Verified on a 4-cell nested tree (Xvfb, probe_handlehint.py): each of
  the three handles lit exactly the cells its rect abuts -- the root
  handle three of them, the nested ones two -- and leaving cleared them.

## 19. A frame consumer per cell (2026-08-28)

The canvas can now host a path-traced cell beside a rasterized one:
`view.cyclesViewport(...)` on a claimed cell traces that cell alone.
The full record is `docs/CyclesIntegration.md` sec 5.11; what changed
on the canvas side is small.

- `Renderer::setFrameConsumer` / `frameConsumerSurface` /
  `setExternalBaseLayer` take a sub-view id. The bgfx backend keeps a
  consumer slot per bank and resolves the one of the submit in
  progress beside `selectSubView`; `dropSubView` drops the slot too.
  A consumer's `hostTarget()` inside a sub-view submit was already the
  bank's target -- the cell -- so the blit needed nothing.
- `ViewAreaCanvas::paintGL` calls
  `View3DInventorViewer::feedCanvasCyclesViewport` for every drawn
  cell before `renderSubViews`, with the cell's camera at the cell's
  size and the feeder: the scene is the FEEDER's render cache, the
  one traversal a canvas runs (sec 13.4), the camera and the render
  settings the cell's own. The frame's background is resolved before
  the loop now, for the same reason.
- A viewer detaches its consumer from the shared backend before every
  swap of `renderer` (adopt, give back, type change) -- the canvas's
  instance outlives a cell that leaves, unlike a lone backend.
