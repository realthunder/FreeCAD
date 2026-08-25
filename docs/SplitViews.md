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

