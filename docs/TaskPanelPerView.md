# Task panels per view

Order (2026-10-06), in the user's words (spelling tidied, one sentence about
scheduling left out):

> Let's build on top on upstream extension of per document taskpanel. Let's
> make it per view first. Meaning each task panel has an owner view. It can
> be hosted in original combo view, in which case, it will auto show/hide
> task panels based on active view. It can also be hosted in the view like
> you suggested which will be independent of active view switching. [...]
> Add one more titlebar button in combo view to switch attachment mode of
> taskbar panels. Note that under view attachment mode, the panels must be
> aware of overlay panel to not overlap. Browser always do per view panel
> like you suggested.

Implementation is the Linux session's (x16); the Windows session tests. This
document is the survey (sec 2), the design (sec 3-7), the constraints that
are not negotiable until other work lands (sec 8), the milestones with what
each is tested by (sec 9), and the readings of the order that were assumed
and want confirming (sec 10). What is built, and where it departs from the
design, is sec 11 to 14: milestone 1, a selection per view, milestone 2,
and milestone 3, the panel inside its view. Milestones 4 and 5 -- the dock
overlays, the browser -- are not started. Sec 15 is the order that came
after milestone 3 -- two modes for the panel in its view, and a state per
view where sec 10 assumed one switch for all -- as a design, not built.

Sec 15 is built through: the state per view (15.9), the panel beside its
view (15.10), the panel over it as an overlaid dock (15.11), clear of the
dock overlays (15.12) and kept for views that are no 3D views (15.13).
What is left of it is in 15.14.

It is step E of `docs/ThinClient.md` 8.12 ("Chrome -- process-global ... per
client, a Control per view, which neither fork has") taken one notch: the
panel gets an owner and a place, `Gui::Control()` stays one object.

## 1. What the user gets

- A task panel belongs to the view it was opened for: the view a sketch is
  edited in, the view a Pad was created in, a browser client's own view.
- **Combo mode** (today's place): the panel lives in the combo view's Tasks
  tab. The tab shows the panel of the ACTIVE view; activate another view and
  it shows that view's panel, or the watchers when it has none. Come back
  and the panel is there as it was left.
- **View mode**: the panel sits inside its owner view, over the picture, and
  stays there whichever view is active. Two cells of a split, each with its
  own panel, are both visible at once.
- One more button on the combo view's title bar switches between the two.
- In view mode the panel keeps out from under the dock overlays.
- A browser client always sees the panel of its own view, never another's.

## 2. Ground truth

Facts the design rests on, with sources. Line numbers are `PartDesignPort`
at `270b0bb4cb` and `upstream/main` at `b960974504`.

### 2.1 The fork today

- `Gui::ControlSingleton` (`src/Gui/Control.{h,cpp}`) holds ONE
  `ActiveDialog`. `showDialog(dlg)` hands it to the combo view
  (`DockWindowManager::getDockWindow("Combo View")`, `ComboView::showDialog`)
  or, with no combo view, to a `TaskView` in a dock of its own
  (`_taskPanel`). `taskPanel()` returns whichever exists. Callers:
  174 `Control().showDialog`, 170 `Control().activeDialog`, 54
  `Control().closeDialog` in C++, and 304 uses of the three from Python.
- `Gui::TaskView::TaskView` (`src/Gui/TaskView/TaskView.{h,cpp}`) is a
  `QWidget` with one `QSint::ActionPanel`, one `ActiveDialog`, one
  `ActiveCtrl` (the button box), the watchers, and `contextualPanels`.
  `addContextualPanel(panel, doc)` takes a document "for source
  compatibility" and ignores it; the header says why: "Upstream keeps one
  task panel per document ... This fork's task view follows the active
  document instead".
- `TaskDialog` has `getDocumentName()` / `setDocumentName()` and
  `autoCloseOnTransactionChange`; it has no associated view, no
  `activate()` / `deactivate()`, and none of upstream's other auto-close
  switches.
- `Control().signalShowDialog` / `signalRemoveDialog` are what
  `Fw::PanelMirror` listens to (`src/Gui/Fw/FwPanelMirror.cpp` 383-389).
- `showDialog` disables `App::AutoTransaction` globally (ThinClient.md 8.12
  E), and the application has ONE active transaction:
  `App::Application::setActiveTransaction` (`src/App/AutoTransaction.cpp`
  131) commits the open transaction of EVERY document before it starts a
  new one. Sec 8 is about this.
- Views: a 3D view is a `View3DInventor` (an `MDIView`) in a
  `ViewAreaCell` of a `Gui::ViewArea`, and the area is the MDI tab
  (`docs/SplitViews.md`). `MainWindow::activeWindow()` is the embedded
  view, never the area (sec 20 there, fixed 2026-10-05). A cell's plain
  child widgets paint above the canvas (sec 16.1, measured), which is what
  the menu button, the zones and the highlight already are.
- A served client's view is a `MirrorViewer` -- a `ViewerContext` with no
  widget and no `MDIView`. `ViewerContext::current()` names the view being
  handled while a `ViewerScope` is open (a client's request, its replayed
  events, an edit's own events); on the desktop outside an edit no scope is
  open.
- `OverlayManager` (`src/Gui/OverlayManager.cpp`) lays four
  `OverlayTabWidget`s over the MDI area as a whole; each keeps the rect it
  occupies in MDI coordinates (`getRect()`, recomputed in `onTimer`, ~680
  on). It knows nothing of cells. It already listens to the task view
  (`onTaskViewUpdate`, 1609) and already keeps clear of the ACTIVE view's
  navigation cube.
- Dock title bars are `OverlayTitleBar`s built by
  `OverlayManager::setupTitleBar` from a list of `QAction`s (`_actOverlay`,
  `_actFloat`, `_actClose`; `OverlayManager.cpp` 409-491, buttons made by
  `OverlayTabWidget::createTitleButton`).
- The browser: `Fw::PanelMirror` walks the real task panel into models
  (`docs/Sandbox.md` 7.19); `SceneWidgetStream` (`src/Gui/SceneWidgets.cpp`)
  sends them to every client subscribed to `panels`; the viewer renders
  them as a floating, draggable card (`web/src/panel.ts`,
  `web/src/widgets/`). One producer, one `Control`, one panel for all.

### 2.2 Upstream today

Upstream made the task panel per DOCUMENT in `f4665aa7b5` "Core: support
multiple active transactions" (2026-02-01, 245 files). The part that
matters here:

- `ControlSingleton`: `showDialog(dlg, App::Document* attachTo = nullptr)`,
  `activeDialog(attachedTo)`, `accept` / `reject` / `closeDialog` /
  `isAllowedAlterDocument` / `...View` / `...Selection` with the same
  argument; null means the active document (`docOrDefault`).
- `TaskView` is a `QStackedWidget`. Page 0 is the watchers' `TaskPanel`;
  each open dialog is a `TaskInfo { TaskPanel*, ActiveDialog, ActiveCtrl,
  Document }` with a page of its own. `slotActiveDocument` shows the active
  document's page (`setShownTaskInfo`), which calls the outgoing dialog's
  `deactivate()` and removes the selection gate, and the incoming one's
  `activate()`.
- `TaskDialog` gained `associateToObject3dView` / `getAssociatedView`,
  `activate()` / `deactivate()`, and auto-close on reset edit, deleted
  document and closed view (`4f323f9580`, `6b890e0d73`, `88ef8b24c6`).
- The same commit makes transactions per document, and a family of others
  scopes selection and the selection gate per document (`3076ce66be`,
  `d59471b6bd`, `7cb3815fb1`).

What is taken from it: the SHAPE -- a stack of per-owner pages, the keyed
`Control` entry points, `activate()` / `deactivate()`, the auto-close
switches, the associated view. What is NOT taken: its transactions (the
fork's answer is `origin/Transaction`, per-user undo) and its per-document
selection (the fork has the `SelectionSingleton` stack and the room,
ThinClient.md 8.4). Port by reading, not by cherry-pick: `f4665aa7b5`
cannot be applied in part.

## 3. The owner

A panel's owner is a VIEW. Upstream's key, the document, is what a view
gives when asked, so everything upstream keys by document still has an
answer.

```cpp
namespace Gui {
/// The view a task panel belongs to. A value, cheap to copy and compare.
class GuiExport TaskOwner {
public:
    TaskOwner() = default;                    // nobody: see "unowned" below
    explicit TaskOwner(MDIView *view);        // a desktop view, any kind
    explicit TaskOwner(ViewerContext *view);  // a 3D view or a client's mirror

    /// The view being handled now: ViewerContext::current() when a scope
    /// is open, else the main window's active view.
    static TaskOwner current();

    MDIView *mdiView() const;            // null for a client's mirror
    ViewerContext *context() const;      // null for a non-3D view
    Gui::Document *document() const;
    bool isRemote() const;               // a client's mirror
    bool isValid() const;                // the view still exists
    bool operator==(const TaskOwner &) const;
    ...
};
}
```

- A desktop 3D view has both faces -- its `View3DInventor` and that view's
  `View3DInventorViewer` -- and they are ONE owner. Normalise on
  construction (a `ViewerContext` that is a desktop viewer becomes its
  `MDIView`), so the two constructors cannot make two keys for one view.
- A non-3D view (a TechDraw page, a spreadsheet) is an owner like any other:
  dialogs are opened for those too.
- A client's mirror has no `MDIView`; the owner is its `ViewerContext`.
- Identity must survive the view being destroyed without dangling: hold a
  `QPointer<MDIView>` for the desktop face, and for a mirror an id the
  serving source can validate (a mirror is deleted when its client goes).
  A dead owner's panel is closed, not orphaned (sec 5.4).
- **Unowned.** A dialog shown with no view to name -- no MDI view open at
  all, or a caller that passes nothing while nothing is active -- gets the
  null owner. It behaves as today: shown in the combo view whatever is
  active. There is at most one.

**Which view opens a panel.** `Control().showDialog(dlg)` with no owner uses
`TaskOwner::current()`. That is right at the sites that matter without
touching them:

- `Document::setEdit` activates the view the edit starts in before
  `startEditing()`, and a view provider's `setEdit` is where its panel is
  shown: the owner is the editing view.
- A client's edit, command or menu choice runs under that client's
  `ViewerScope`: the owner is its mirror.
- A command run from a toolbar or a shortcut acts on the active view.

The one path that is wrong by default is a DEFERRED show -- a timer, a
queued call -- which runs with no scope open and names whichever view is
active by then. ThinClient.md 8.10 names the hazard, and
`Gui::Document::resetEdit` opening its own scope is the precedent: a
deferred `showDialog` must carry its owner (capture `TaskOwner::current()`
when the deferral is made, pass it to `showDialog`). Sites to audit are in
sec 9, M1.

## 4. Control and the task view

### 4.1 Control

```cpp
void showDialog(TaskView::TaskDialog *dlg, const TaskOwner &owner = TaskOwner());
TaskView::TaskDialog *activeDialog() const;                  // see below
TaskView::TaskDialog *activeDialog(const TaskOwner &) const; // that view's
void accept(const TaskOwner & = TaskOwner());
void reject(const TaskOwner & = TaskOwner());
void closeDialog(const TaskOwner & = TaskOwner());
bool isAllowedAlterDocument(const TaskOwner & = TaskOwner()) const;   // + View, Selection

// Upstream's signatures, for code ported from there: the dialog of the
// document's views (its active view's first).
void showDialog(TaskView::TaskDialog *dlg, App::Document *attachTo);
TaskView::TaskDialog *activeDialog(App::Document *attachedTo) const;
...
```

Python: `Gui.Control.showDialog(dlg, view=None)`, `activeDialog(view=None)`,
`closeDialog(view=None)`, and upstream's `attachTo=` keyword accepted as the
document form.

**What the argument-less `activeDialog()` answers is a policy, and it
changes once** (sec 8): while only one dialog may be open in the process it
answers THAT dialog whichever view is active, exactly as today, so that the
170 callers that mean "is anything open?" keep their meaning. When several
may be open it answers the current owner's. One private function decides,
`ControlSingleton::exclusive()`; nothing else in the design depends on
which way it says.

`signalShowDialog` / `signalRemoveDialog` gain the owner as an argument.
New: `signalDialogActivated(owner)` when the panel SHOWN in the combo view
changes, and `signalHostChanged()` when the mode is switched.

### 4.2 The task view

Upstream's structure, keyed by owner:

```cpp
struct TaskInfo {
    TaskPanel *taskPanel;        // the page: content + button box
    TaskDialog *ActiveDialog;
    TaskEditControl *ActiveCtrl;
    TaskOwner owner;
};
```

- `TaskView` becomes the stack: page 0 the watchers, one page per open
  dialog. Port `TaskPanel`, `showDialog` / `removeDialog` per entry,
  `setShownTaskInfo`, `currentTaskInfo`, and the lambda-per-entry button
  wiring (`accept(owner)` and the rest) from upstream's `TaskView.cpp`
  593-930.
- The fork's own additions stay: `eventFilter`, the status timer,
  `contextualPanels` (which now really are per owner, as upstream's are per
  document -- `addContextualPanel(panel, doc)` stops ignoring its second
  argument), `takeTaskWatcher`, the action style calls.
- `TaskDialog` gains `owner()` (set by `showDialog`), `activate()` /
  `deactivate()` (virtual, empty by default), and upstream's auto-close
  switches: on reset edit, on deleted document, on closed view.
  `getAssociatedView()` is the owner's `MDIView`.
- A page is a self-contained widget: content and button box travel
  together. That is what makes it movable between hosts (sec 5).

### 4.3 Activation

`activate()` / `deactivate()` are for state a dialog holds outside its own
widgets and that only one dialog can hold at a time: a selection gate, an
event filter on the main window, a cursor. Upstream removes the selection
gate on the way out (`setShownTaskInfo`); do the same, and let the dialog
put its gate back in `activate()`.

In the fork the gate is per `SelectionSingleton` instance and a client's
view has an instance of its own, so a client-owned dialog's gate never
collides with a desktop one's. Two desktop views share the room, and there
the rule above is needed.

A dialog is ACTIVE when its owner is the current owner, in either hosting
mode -- activation follows the view, not the place the panel is drawn.
A view-hosted panel that is not active stays visible and usable: a click
into it makes its view the active one first (sec 5.2), which activates it.

## 5. Hosting

### 5.1 Combo mode

The stack lives in the combo view's Tasks tab (or the standalone task
dock), as upstream's does. `TaskView` listens to
`Application::signalActivateView` -- not `signalActiveDocument`: two views
of one document are two owners -- and shows the active view's page, or
page 0. Unowned shows over everything.

- Switching views must not steal the tab: the combo view raises its Tasks
  tab when a dialog is SHOWN for the first time (as now), not on every
  activation.
- The tab's "busy" icon (`aboutToShowDialog`, `edit-edit.svg`) is on while
  the shown page is a dialog, off on page 0.
- A panel whose owner is not active is alive and hidden. Nothing in it may
  assume it is visible: audit `showEvent` / `hideEvent` overrides in task
  boxes that start or stop something (sec 9, M2).

### 5.2 View mode

Each owner view that has a dialog gets a HOST: a child widget of the view's
`ViewAreaCell`, above the canvas, holding that dialog's page.

- `ViewAreaCell` gains `taskHost()` (created on demand, destroyed with the
  cell) and lays it out in `resizeEvent` with the other chrome. A view not
  in a `ViewArea` (`UseViewArea` off, or an undocked top-level view) hosts
  in the `MDIView` itself by the same code: the host's parent is "the
  widget the view fills", which is the cell when there is one.
- Default place: the left edge, full height less a margin, width the page's
  size hint clamped to a third of the cell; the watchers do not move in --
  page 0 stays in the combo view.
- The host has a slim header: the dialog's title, a collapse button (down
  to the header), a "send to combo view" button that switches the mode. It
  can be dragged along the edges of its cell, and remembers side and
  collapsed state per view kind in the user parameters. Borrow the dock
  overlay's style sheet (`OverlayManager` applies one per tab widget) for
  the translucent look rather than inventing a second one.
- Input: the host is an ordinary widget, so its children take the mouse and
  the keys over the canvas as any child of the cell does. `TaskView::
  keyPressEvent` (Enter / Escape to the button box, and the deferred
  `resetEdit` for a panel with no Cancel) moves to the page or is installed
  on the host too -- it must act on THAT page's dialog, not on "the"
  active one. `ViewArea::onFocusChanged` already makes a cell active when
  the focus moves into it, and the host is inside the cell, so a click in a
  panel activates its view with no new code; verify it (sec 9, M3).
- A view too small for the panel (a cell split down to a sliver) collapses
  the host to its header rather than covering the view.
- **A client's mirror has no widget.** In view mode a remote-owned panel
  has nowhere on the desktop to go, and wants none: its page is created
  parentless and never shown, as the on-view parameter boxes are
  (ThinClient.md 8.7) -- the panel mirror walks a widget tree, shown or
  not. In combo mode it is likewise never the shown page, because a mirror
  is never the desktop's active view.

### 5.3 Keeping clear of the dock overlays

"The panels must be aware of overlay panel to not overlap": the dock
overlays are laid over the MDI area, a host is laid in a cell, and where a
cell touches an edge that has an overlaid dock the two would cover each
other.

- `OverlayManager` gains `QRegion occupied(const QWidget *relativeTo)
  const`: the rects of the overlaid tab widgets that are visible and not
  auto-hidden (`getState() <= Normal`, the test `onTimer` already makes for
  `rectBottom` / `rectLeft`), mapped into `relativeTo`'s coordinates. Empty
  when the overlay is off.
- The cell lays its host in `cell->rect()` minus that region: the host
  moves to the other side when its side is taken, and shrinks when both
  are. Re-laid when the overlay's layout changes: add a
  `OverlayManager::layoutChanged()` signal at the end of `onTimer`'s
  geometry pass (emit only when a rect changed).
- The other direction comes free: the dock overlays already treat child
  widgets under the cursor as theirs or not by hit test, and the host is
  not a dock. Check that mouse pass-through
  (`DockOverlayAutoMouseThrough`) does not send a click meant for a host to
  the canvas, and that an auto-hidden overlay's reveal strip is not hidden
  under a host (the host yields: its geometry never includes the hint
  strip, `DockOverlayHintSize`).
- The Tasks dock itself may be overlaid in combo mode; that is today's
  behaviour and is not changed. In view mode the Tasks tab holds only the
  watchers, and `onTaskViewUpdate` (which shows or hides the overlaid Tasks
  dock as the task view fills or empties) must count page 0 only.

### 5.4 Lifecycle

- A view closes while it owns a dialog: `autoClosedOnClosedView()` then
  remove, as upstream's `slotViewClosed`. A document closes: the same per
  owner view of it. A client disconnects: its mirror goes, its dialog is
  rejected (not accepted: nobody is there to answer).
- An edit ends: `autoClosedOnResetEdit` for the dialog of the EDIT'S view
  -- `Gui::Document::editingViewer()` names it -- not of the active view.
- The mode is switched: every page is re-parented to its new host in one
  pass; no dialog is closed or reopened, no `activate()` / `deactivate()`
  is called (the active owner did not change). Focus inside a moved page is
  put back.
- A view is moved between cells or undocked: its host goes with it
  (`ViewAreaCell::hostView` / `releaseView` hand the page to the new
  parent).
- A cell is maximized: hosts of hidden cells are hidden with them.

### 5.5 The switch

- `ViewParams` (or `OverlayParams`, where the dock chrome's are):
  `TaskPanelInView`, bool, default OFF -- combo mode is today's behaviour
  and stays the default until the view mode has been used for a while.
- A title bar button on the dock that holds the task view: one more
  `QAction` in the list `OverlayManager::setupTitleBar` builds from
  (`_actTaskHost`, data `"OBTN TaskHost"`, a `qss:overlay/` icon pair for
  the two states, tool tip "Show task panels in their views" / "... in the
  combo view"), added only for that dock. Checked state follows the
  parameter; the parameter is the truth, so the preference page and the
  host header's button are the same switch.
- One mode for all panels (assumed, sec 10).

## 6. The browser

A client always gets the panel of ITS view, whatever the desktop's mode.

- `Fw::PanelMirror` mirrors every open dialog, not "the" one: the `panel`
  list object's layout becomes the roots of all of them, each root model
  carrying `owner` -- the client id for a mirror-owned dialog, empty for a
  desktop-owned one -- and `ownerView` for display. Ids are unchanged
  (`panel:<n>`, `pw:<n>`).
- `SceneWidgetStream::wants(client, id)` learns ownership: a client
  subscribed to `panels` is sent the roots it owns and their subtrees, and
  `widgets.subscribe`'s reply names its own panel in `panel`. The shared
  session of ThinClient.md 8.11 is the exception that stays: while a
  document is in a SHARED edit every client in the session is sent the
  session's panel (whoever answers it answers it), as now; with
  `PerViewEdit` only the session's own view is. That is the same rule
  `joinEditing` / `announceEdit` already follow -- ask
  `EditingRoot::isShared()`.
- Top-level dialogs (`dialog:<n>`, a panel slot's message box) belong to
  the panel whose slot raised them. The mirror cannot know that from the
  window alone; take the owner of the dialog that is ACTIVE when the window
  shows, which under a client's scope is that client's.
- A client's write comes back through the store into the real widget
  (unchanged); `accept` / `reject` from a client act on its own dialog --
  `FwPanelMirror.cpp` 1551/1555 call `Control().accept()` bare and must
  pass the owner of the root the request named.
- The viewer needs no layout work for this: the panel is already a floating
  card over the client's one view. With several sub-views in one page
  (`docs/SplitViews.md` 11) the card stays per client, not per sub-view --
  a client is one mirror.

## 7. What else reads "the" dialog

- `Gui::Document::setEdit` (`Document.cpp` 784) names the document on the
  active dialog; it wants the dialog of the edit's view.
- `Command::isActive()` implementations and `Document.cpp` ~4700 ask
  `isAllowedAlterDocument()` / `activeDialog()` to refuse while a dialog is
  open. Under sec 8's rule they keep their meaning. The list to revisit
  when the rule is lifted is every bare `activeDialog()` -- 170 in C++ --
  and is not part of this work.
- `OverlayManager::onTaskViewUpdate` and `TaskView::isEmpty()`.
- The sketcher's `ViewProviderSketch::setEdit` reuses an open
  `TaskDlgEditSketch` (`qobject_cast` on `activeDialog()`); it must ask for
  its own view's.
- Python: `Gui.Control.activeDialog()` in workbench code that polls it
  (Draft, BIM, CAM task panels) -- unchanged in meaning under sec 8.

## 8. One dialog at a time, for now

The fork has ONE active transaction for the whole application, and opening
another commits every document's open one
(`App::Application::setActiveTransaction`). A task dialog typically opens a
transaction and holds it until OK or Cancel. Two dialogs open at once --
in two views of one document, or in two documents -- would have the second
commit the first one's work in progress, and the first one's Cancel would
then roll back nothing. Upstream bought the right to per-document panels
with per-document transactions in the same commit; the fork's equivalent is
`origin/Transaction` (per-user undo, ThinClient.md 8.12 G), not merged.

So until that lands:

- **At most one dialog is open in the process** (`ControlSingleton::
  exclusive()` true). `showDialog` for a second owner is refused exactly as
  a second dialog is refused today, with the same warning.
- Everything in sec 3-6 is built for N anyway -- the stack, the keyed
  entry points, the hosts, the mirror's ownership -- because that is the
  only way the per-view behaviour can be right: the one dialog has an
  owner, is shown in the combo view only while its owner is active (page 0
  otherwise, with a line saying which view holds a panel and a button that
  activates it), sits in its owner's cell in view mode, and reaches only
  its owner's client.
- The tests of M1-M4 that need two dialogs alive drive the stack with
  `exclusive()` switched off by a test-only parameter
  (`TaskPanelAllowConcurrent`, hidden, default off) using dialogs that open
  no transaction. That keeps the N path exercised without shipping it.
- Lifting it is M6 and is a separate decision: it needs a transaction per
  owner, and the audit of sec 7.

If the user wants several live dialogs sooner than `origin/Transaction`,
the cheapest honest form is: allowed only for dialogs that declare they
hold no transaction (`TaskDialog::holdsTransaction()`, default true).
Not designed further here.

## 9. Milestones

Each is a commit series that leaves both suites green, with a GUI test in
`tests/gui/` scored against the tree before (A/B), as the per-view edit
work was. Tests that depend on a panel being destroyed must return to the
main loop between steps: a `deleteLater()` is not run by an event loop
nested in the function that asked for it (see
`tests/gui/pd-origin-in-edit.py`).

- **M1. Owner and keyed Control; no visible change.** `TaskOwner`;
  `Control` entry points with an owner; `TaskDialog::owner()`, upstream's
  signatures as overloads; Python keywords; signals carry the owner.
  `TaskView` still shows the one dialog as today. Audit deferred
  `showDialog` calls (grep `singleShot`/`QueuedConnection` near
  `showDialog`; the sketcher's validation dialog, PartDesign's feature
  pick, Python `QTimer` shows in Draft/BIM) and make them carry their
  owner. Test: a dialog opened by a command in view A reports A; one opened
  by `setEdit` in view B while A is active reports B; one opened under a
  client's scope reports the mirror; a deferred show keeps the owner it was
  asked under.
- **M2. The stack, combo mode.** `TaskView` as upstream's stack keyed by
  owner; `activate()` / `deactivate()`; auto-close on closed view, deleted
  document, reset edit; `contextualPanels` per owner. Combo view shows the
  active view's page. Test (two cells of one document, a sketch edited in
  one with `PerViewEdit` on): the Tasks tab shows the sketch panel while
  the editing view is active, page 0 with the hint when the other is,
  the panel again -- same widgets, same state -- on return; the selection
  gate follows; closing the editing view rejects the panel. With the
  test-only concurrency switch: two transaction-less dialogs in two views,
  each shown for its own.
- **M3. View mode.** `ViewAreaCell::taskHost()`, the host widget, the
  parameter, the title bar button, re-parenting on switch, focus and keys
  per page. Test: with the mode on, the sketch panel is a child of the
  editing cell and stays visible when the other cell is active; a value
  typed into it lands in the sketch; Escape and Enter act on that panel;
  the title bar button and the host's button both flip the parameter and
  move the page without closing the dialog (the dialog object is the
  same, the edit is still on); a view with no `ViewArea` hosts in itself.
- **M4. Dock overlays.** `OverlayManager::occupied()`, `layoutChanged()`,
  the host's layout against them, pass-through and reveal checks. Test:
  with the tree overlaid on the left, the host's geometry does not
  intersect the tree's rect and moves when the overlay is toggled; with
  left and right both overlaid it shrinks between them; an auto-hidden
  overlay's hint strip is not covered.
- **M5. The browser.** The mirror's ownership, the stream's gate, a
  client's accept/reject on its own root. Tests on the model of
  `tests/gui/serve-per-view-edit.py`: a desktop window and two clients,
  `PerViewEdit` on -- a client's sketch panel reaches that client only, the
  desktop's reaches no client; with a shared session it reaches every
  client in it, as now; a client's OK closes its own edit. Then a real
  browser, by hand.
- **M6 (not scheduled). Several at once.** After `origin/Transaction`.

Docs to keep in step: `docs/ThinClient.md` 8.12 item E, `docs/SplitViews.md`
(the cell's new chrome), `docs/Sandbox.md` 7.19 (the mirror's list).

## 10. Assumed, to be confirmed by the user

1. "Overlay panel" is the DOCK overlay (tree, property view, report view
   laid over the MDI area). Sec 5.3 is written for that.
2. The title bar button is ONE mode for all panels, not a per-panel pin.
3. The watchers (the workbench's task watcher boxes shown when no dialog is
   up) stay in the combo view in both modes.
4. Combo mode stays the default until the view mode has been used.
5. One dialog at a time until `origin/Transaction` (sec 8) -- the per-view
   behaviour is real from M2, several live dialogs are not.
6. In a SHARED edit session every client in the session still sees the
   session's panel; "always per view" is literal only under `PerViewEdit`.

## 11. Built

### 11.1 Milestone 1 (2026-10-06, `68f95d11fd`)

The owner and the keyed `Control`. Nothing visible changed: the task view
still shows the one dialog, whichever view is active.

- `Gui::TaskOwner` (`src/Gui/TaskOwner.{h,cpp}`) as sec 3 has it, with two
  things settled that the sketch left open:
  - a mirror's liveness is `ViewerContext::lifetime()`, a `weak_ptr` every
    view context now hands out (expired first thing in its destructor).
    No id for the serving source to validate was needed, and the same
    token is the mirror's identity: a dead view's owners still equal each
    other, and equal no view that came to stand at its address. A desktop
    view is a `QPointer` plus the address it was made with, compared
    together for the same reason.
  - a 3D viewer that sits in no `MDIView` (one made inside a dialog) is an
    owner of the mirror's kind that is not remote. `isRemote()` asks the
    context (`cameraIsRemote()`), not the kind.
- `Control`: `showDialog(dlg, owner)`, and `activeDialog` / `accept` /
  `reject` / `closeDialog` / `isAllowedAlter*` with an owner or with
  upstream's `App::Document *`. One private function, `dialogOf(owner)`,
  answers all of them; `exclusive()` returns true and nothing else reads it.
  - **An unowned dialog is everybody's**: asked for any view, it is that
    view's. That is what "behaves as today" has to mean for the keyed
    forms.
  - `accept` / `reject` / `closeDialog` are OVERLOADS beside the
    argument-less slots, not one function with a default argument as sec
    4.1 writes them: a slot with a defaulted argument cannot be connected
    by member pointer, and five sites do
    `QTimer::singleShot(..., &ControlSingleton::closeDialog)`. They name
    the slot with `qOverload<>` now.
  - `showDialog(dlg, App::Document *)` takes the view being handled when it
    is one of that document's (a served document may have no desktop view,
    and under a client's scope the asking view is that client's), else the
    document's active view, else nobody.
- `TaskDialog::owner()`, set once by `showDialog` before the dialog's
  `open()`; `getAssociatedView()`. A dialog shown again keeps the view it
  was first shown for.
- `signalShowDialog` / `signalRemoveDialog` carry the owner as a third
  argument. Both listeners (`Fw::PanelMirror`, PartDesign's `Monitor`) take
  it and do not use it yet.
- Python: `view=` and `attachTo=` on `Gui.Control.showDialog`,
  `activeDialog`, `activeTaskDialog` and `closeDialog`; a task dialog
  object has `getAssociatedView()` and `getOwnerKind()` (`'view'`,
  `'client'`, `'viewer'`, `'none'`, `'gone'`).
  - **Not in the design: `Gui.Control.currentOwner()`.** A deferred Python
    show has to carry its owner, and a client's view has no Python object
    to be named by. This returns an opaque token for
    `TaskOwner::current()` that `view=` accepts beside a view object.

**The audit of deferred shows.** C++: none. Every file that both shows a
dialog and defers something was read; the timers there do other work
(a delayed selection clear, a delayed focus). Python: four, all Draft's,
through its `todo` queue -- `DraftGui.py` `taskUi` and the base-widget
panel, `gui_selectplane.py`, `gui_scale.py`. They take `currentOwner()`
when the show is queued and pass it (sec 11.2). BIM's and CAM's timers
near a `showDialog` show a form or a status widget, not a task dialog.

Found on the way, not changed:

- **Deferred CLOSES and ACCEPTS.** Five `singleShot` closes (Surface x2,
  PartDesign's feature pick x2, Part's mirror) and two queued
  `invokeMethod(&Control(), "accept")` in the feature pick act on "the"
  dialog. Right while `exclusive()` holds; they join the list of sec 7 for
  M6. They cannot simply take `TaskOwner::current()`: the task box that
  queues them does not know its dialog, and the view active at that moment
  need not be the dialog's.
- `gui_selectplane.py` and `gui_scale.py` call `setDocumentName` and
  `setAutoCloseOnDeletedDocument` on what `showDialog` returns, which in
  this fork is None: an AttributeError the `todo` queue swallows with a
  warning, after the panel is up. `DraftGui.py` guards it. M2 brings the
  auto-close switches, and `showDialog` returning the dialog object as
  upstream's does belongs with them.
- `ControlSingleton::showDialog`'s branch for a main window with no combo
  view never sets the active dialog, so `activeDialog()` is null there
  with a panel on screen. Older than this work; the keyed forms inherit it.
- `Gui::Document::setEdit` still names the document on the argument-less
  `activeDialog()` (sec 7). It is the same dialog while `exclusive()`
  holds; it moves to the edit view's with the stack, M2.

Test: `tests/gui/task-panel-owner.py` (`GuiTaskPanelOwner_tests_run`), two
views of one document, a second document, one served client: 34 checks. On
the tree before (`0259b90df5`) 9 of 35 pass, the ones that name no view;
the rest fail on the missing keyword or method. What it does NOT cover: a
deferred show under a CLIENT's scope -- a client may run only allowlisted
commands and none of them queues a panel, so the owner crossing a timer is
shown with two desktop views; and two clients told apart, which wants the
client id a mirror does not know yet (M5).

### 11.2 Draft's deferred shows (2026-10-06)

The four sites the audit found. Draft queues a panel's show on its `todo`
list, which a zero timer runs after the command has returned; each site
now takes `Gui.Control.currentOwner()` where it queues and passes it as
`view=` where the queue runs.

Measured, on the milestone 1 binaries with the Draft files before and
after (`tests/gui/draft-panel-owner.py`, `GuiDraftPanelOwner_tests_run`):
a command run with view a1 active, a2 activated before control returns to
the event loop.

| | before | after |
|---|---|---|
| `Draft_SelectPlane` (`gui_selectplane.py`) | a2's | a1's |
| `Draft_Line` (`DraftGui.py`, `taskUi`) | a2's | a1's |

So the hazard of sec 3 is real on the desktop too, not only for a client:
two views, a click in the other one inside the same event-loop pass.
`gui_scale.py`'s second panel has the same change and is not exercised --
it needs two picked points. The `setDocumentName` on None noted in 11.1
still prints its warning in both columns.

### 11.3 The suites

On `68f95d11fd` (milestone 1 on the merge of PartDesignPort `84c14e12d5`):
Python 3379 tests OK; ctest 953 of 953 (962 entries, 3819 s serial); the
GUI gate 70 tests OK, the panel mirror's module among them. Rows in
`docs/Testing.md`.

### 11.4 Milestone 2, first half: the stack (2026-10-06, `c503f2a1a0`)

`TaskView` holds a page per open dialog, as sec 4.2 has it, and still
shows the dialog's page whenever there is one: the structure of milestone
2 with none of its behaviour. Following the active view, `activate()` /
`deactivate()`, the auto-close switches and the hint on page 0 are the
second half.

- `TaskView` stays a `QWidget` with its layout and margins; a
  `QStackedWidget` stands where its one scroll area stood. Page 0 is that
  scroll area with the watchers' panel. `TaskPage` is a dialog's page:
  its own scroll area and `TaskPanel`, and the button box in the panel or
  pinned outside the scrolling (`StickyTaskControl`) -- content and
  buttons in one widget, which is what sec 5 moves between hosts.
- `TaskInfo { page, ActiveDialog, ActiveCtrl, owner, contents }`, a vector
  of them (`taskInfos`), found by dialog; `ActiveDialog`, `ActiveCtrl` and
  `contents` are gone from the class. The button wiring is a lambda per
  dialog. `dialog(owner)` and `currentTaskInfo()` are public.
- The argument-less `accept()` / `reject()` / `removeDialog()` act on the
  shown page's dialog, else the only one.
- `Control().signalShowDialog` / `signalRemoveDialog` hand listeners the
  dialog's PAGE where they handed the task view. Both listeners want
  exactly that: the panel mirror looks in it for the button box, and
  PartDesign's monitor parents a widget to it.
- **Contextual panels** (`addContextualPanel`; Assembly's solver panel is
  the one user) sit in the panel of the page that is SHOWN and are carried
  over when the shown page changes, at the top as before. Per owner is the
  second half's.
- A removed page is deleted when its dialog has been: the dialog owns its
  content widgets and deletes them, and until then they are the page's
  children, as they were the one shared panel's.

**Nothing visible changed -- measured, and it nearly had.** A probe took
the place and size of every visible widget in the task view, and the task
view's picture, in seven states (the watchers, a Pad's dialog with the
button box in the panel and pinned, a sketch's, a Python panel's, the
watchers again twice) on the tree before and after. First run: the
watchers identical, but every dialog WIDER -- the task view 420 px for the
Pad where it had been 146, the dock grown to fit the content instead of
scrolling it. Cause: `QScrollArea::sizeHint()` takes its widget's size
hint the first time it is asked and keeps it. The one shared scroll area
was first asked at start-up, empty, so its hint stayed 18 x 18 for good
and no dialog ever asked the dock for room; a new page's was first asked
with the dialog in it. "The task view scrolls what does not fit and never
widens its dock" was an accident of that cache. A page now asks while it
is empty, and takes the watchers' current minimum width. Second run: all
seven states the same, widget for widget, 0 pixels differing. Compared in
the test session's narrow dock only (146 px); a wide dock was not.

Letting the dock follow the content, as upstream does (with a width it
restores afterwards), is a choice now, where before it was not possible;
not made here.

Run on it: every GUI and widget entry of ctest (`-R '^Gui|FormWidgets|
QuantitySpinBox'`), 172 of 172, 3267 s; the GUI gate, 70 tests OK; both
owner tests. NOT run on it: the Python suite and the rest of ctest, which
do not build a task view.

### 11.5 Put to the user before the second half

*Answered by sec 12 and built in sec 13: the gate went with the selection
a view has of its own, and a view's closing is now said
(`Application::signalCloseView`). Kept as written at the time.*

The second half of milestone 2 is where behaviour changes, and one point
of sec 4.3 does not survive contact with the fork as written:

- **The selection gate.** Upstream removes the gate when a dialog's page
  is left and expects the dialog's `activate()` to put it back. No dialog
  in this fork does: 32 files add a gate, once, and every one of them
  would lose it the first time the user looked at another view. Proposed
  instead: the task view TAKES the gate out of the selection when the
  page is left and puts the same gate back when it is returned to
  (`SelectionSingleton` has one `ActiveGate` per instance,
  `Selection/Selection.cpp`; it needs a take and a restore beside add
  and remove). `activate()` / `deactivate()` stay, empty, for the state a
  dialog holds that the framework cannot see.
- **While the gate is out**, what the dialog's own selection observers
  were promised never to see can arrive: a sketch's panel hearing of a
  selection in another view. That is the audit of sec 5.1 ("nothing in it
  may assume it is visible"), and it is the larger part of the work.
- There is no view-closed signal in `Gui::Application` yet (upstream's
  `slotViewClosed` listens to one); auto-close on a closed view needs it.

## 12. A selection per view (built 2026-10-06, `e81747aafd`)

Ordered between the two halves of milestone 2 (user, 2026-10-06), and it
replaces the selection-gate proposal of 11.5: with the selection a view's
own, its gate is too, and nothing has to be taken out and put back.

### 12.1 The order, in the user's words

> we need to do something about the selection. each edit session shall
> have their own selection instance to avoid leaking.

> if there is active view that is in editing, tree view selection should
> route to the selection instance for that view. 3d view selection knows
> which view it is originated. if it is from a view that is in edit,
> route to that instance.

> About dialogs, yes if it is intended for editing. The tree view two way
> sync only applies to desktop. Client views selection whether in edit or
> not shall not affect tree view. [...] Maybe we should give each view a
> selection instance by default.

> 1, opt out, 2, always entered on copy of the room selector, because
> that's what they expect today, they can later on clean it if desired.

(1: every task dialog gives its view an instance unless the dialog opts
out. 2: an edit and a dialog start on a copy of the shared selection.)

### 12.2 The model

- **The view answers which instance it selects into.** A view of the main
  window has a selection instance of its own while it has a reason to:
  it is in an edit, or it owns a task dialog (`MDIView::takeOwnSelection`
  / `releaseOwnSelection`, counted, so an edit with its dialog is one
  instance). Otherwise it shares the room, as every view did. A served
  client's view has had its own all along; nothing changes for it.
- **The preference `PerViewSelection`** (View, default off) gives every
  view its own from the start. One mechanism, two defaults: the shared
  default keeps "select in one view, act in another" working from the 3D
  views -- 72 sites ask for the selection of every document, the Link
  commands among them -- and keeps a selection lit in every view of its
  document. Read when a view is made.
- **Entered on a copy, handed back at the end.** The first take copies
  the room's selection (the entries themselves, picked points included).
  The last release copies the view's selection back into the room: a
  sketch stays selected after it is closed, for every view, ready for the
  next command. *The hand-back is not in the order; it is what keeps
  today's behaviour whole, and wants confirming.* What another view
  selected in the room meanwhile is replaced by it.
- **An edit session's instance is its initiator's** -- unchanged from
  `docs/ThinClient.md` 8.11: a view that joins a shared session selects
  into the initiating view's instance. What is new is that a desktop
  initiator has one.
- **3D picks go by the view they come from.** Every Coin event a desktop
  viewer handles is handled with that view's instance current: the
  session's in an edit, its own or the room outside one
  (`View3DInventorViewer::processSoEvent`; the delayed preselection of a
  hover fires from a timer and opens the same scope).
- **Everything with no view of its own goes by the ACTIVE view.** The
  tree, a toolbar command, a shortcut, the Python console: with no scope
  open, `Gui::Selection()` is the active view's instance
  (`SelectionSingleton::setAmbient`, set by
  `MDIView::updateAmbientSelection` when a view is activated, takes or
  releases an instance, or enters or leaves a session). A client's mirror
  is never the active view of the main window, so a client's selection
  never reaches the tree.
- **The panels show the active view's selection.** The tree, the property
  view and the selection view FOLLOW: they are attached to the active
  view's instance, moved when another view is activated, and told to read
  the selection again.

### 12.3 Observers

The larger part of the work. Before, every observer attached to the room
by rule, and only the sketch's view provider bound itself to its
session's instance. Now an observer is one of three things:

- **A follower** hears the active view's instance and is moved with it
  (`followSelection()`). The default with no scope open -- which is what
  a workbench's or an addon's global observer is, Python's included --
  and what the three panels ask for explicitly.
- **Bound** to one instance (`bindSelection`, `attachSelectionToCurrent`),
  and so by default when it is built while a scope has a view's own
  instance current: an edit is STARTED inside its view's scope
  (`Gui::Document::setEdit`), so the task boxes and the edit-mode
  observers built there belong to that view. A viewer is bound to the
  instance it selects into and rebinds when that changes.
- **Adopted** (`adoptSelection`): given a home. `Control().showDialog`
  finds every observer in the dialog and the widgets it shows and gives
  it the owner view's instance -- the ones listening move, the ones that
  listen only while a button of theirs is down attach there when they do.

Moving an observer tells it `RmvPreselect` and `SetSelection` ("read the
selection again"), the message an observer is already sent when too many
changes pile up.

Three things that make it hold:

- **The notifying instance is the current one** for the extent of its
  notification. Of about 1500 `Gui::Selection()` sites many are inside an
  observer, and "the selection" there has to be the one that changed, not
  whichever view is active.
- **An instance that ends lets go of its observers**: they fall back to
  following, except one filtered to an object, which is dropped rather
  than handed to another instance with its gate.
- **An instance is retired, not deleted, while a scope still names it**
  (`SelectionSingleton::retire`): leaving an edit releases the instance
  from inside the scope that made it current.

The nine observers of the old kind (`Base::Observer`, attached to the
room and not movable: the task view's watchers, the sketcher's general
box, the material dialogs) are told of the active view's selection
THROUGH the room, and of no other.

### 12.4 Measured

`tests/gui/selection-per-view.py` (`GuiSelectionPerView_tests_run`): one
document, two views, a dialog and then an edit in one of them; then a
second document with the preference on. 26 checks. On the tree before
(`76d3a68d1e`), 13 of the first 23 fail, every one a leak between the two
views:

| | before | after |
|---|---|---|
| what a1 selects, read with a2 active | selected | not |
| a clear with a2 active | clears a1's | leaves it |
| the tree, a global Python observer | one selection | the active view's |
| a gate set for a1's dialog | gates a2 | gates a1 only |
| a click in a2 | lands in a1 too | a2's |
| the highlight of what a1 selects | drawn in both | drawn in a1 |
| in an edit: a1's picks from a2, a2's from a1 | shared | apart |

The click, the tree and the picture are the real ones: synthetic mouse
events on the view's own widget, the tree's selected items, the
renderer's framebuffer in mode 3.

`SelectionStack_tests_run` grew from 10 to 17 cases. One was RESTATED:
`anObserverBuiltInsideAScopeStillHearsTheRoom` pinned the rule this
replaces, and is now `anObserverBuiltInsideAViewsScopeBelongsToThatView`.
The rest are new: following, binding, adoption, an instance ending, one
retired inside its scope.

Suites on it: Python 3379 OK; the GUI gate 70 OK; full ctest 954 of 955
(964 entries, 3782 s serial), the one failure being that restated case,
which was then rebuilt with the seven new ones and the preference's check
box -- 17 of 17, and the 11 entries nearest the change again, on that
build.

### 12.5 What it does not do

- **Render cache mode 0.** A view's highlight is per view in mode 3,
  where each view's cache manager holds it. In plain Coin the highlight
  state lives in scene nodes every view of the document shares
  (`docs/ThinClient.md` 8.12 C, "one shared graph cannot carry N
  highlights"): two views with different selections draw whichever
  applied last.
- **A dialog used while its view is not the active one.** Its widgets'
  slots run with no scope open and reach the ACTIVE view's instance, and
  an observer it builds late follows instead of being adopted. In combo
  mode the panel is only shown while its view is active once the second
  half of milestone 2 is in; view mode (M3) needs a scope around the
  events delivered to a page.
- **Observers a dialog keeps outside its widgets** (a plain member, a
  Python observer registered by the panel) are not found by the adoption
  and follow the active view.
- **A Python panel cannot opt out** (`TaskDialog::usesOwnSelection` is
  C++ only so far), and no dialog opts out yet.
- **A shared session a client is in.** The session's instance is one for
  every view in it, by 8.11: while the desktop's active view is in such a
  session, the tree shows the session's selection, a client's picks in
  it included. A client's OWN selection never reaches the tree.
- **Deferred calls** run with no scope and take the active view's
  instance, the hazard of sec 3.
- The 31 sites that name the room on purpose (`SelectionRoom()`) were not
  reviewed against the new meaning of "the room": the instance of the
  views that have none of their own.

### 12.6 Sec 8 corrected: what `origin/Transaction` gives

Sec 8 and M6 say several live dialogs wait on `origin/Transaction`. Read
on 2026-10-06 at `7d2c9e0a23`: it does not lift the limit.
`Application::setActiveTransaction` still commits every document's open
transaction before it opens the next, and the branch's own
`docs/TransactionLog.md` lists "one edit slot: an open transaction or
edit session is everyone's" under what it does not do (30.10). What it
adds is per-author undo of COMMITTED steps over the transaction log. A
transaction per session is new work, to be built on that branch's model
-- so the merge has to come before M6, not that M6 comes with it. The
merge is its own job: 482 commits, a trial merge conflicting in 94 files
(41 Sketcher, 23 Gui, 7 App), and almost nothing in the files of this
document.

## 13. Milestone 2, second half: the task view follows the active view (2026-10-06, `48123ff033`)

The behaviour of milestone 2, on the stack of sec 11.4 and the selection
of sec 12. Combo mode is now what sec 1 says it is.

### 13.1 What the user sees

- The Tasks tab shows the panel of the ACTIVE view. Activate another view
  and it shows the watchers, as with no dialog open; come back and the
  panel is there as it was left -- the same widgets, what was typed in
  them, where it was scrolled.
- While the page shown is the watchers' and a dialog is open for another
  view, a box above the watchers says so: "Task panel in another view",
  with a button per such view, "Go to <view title>", that makes that view
  the active one. A dialog a served client owns has a line and no button:
  the main window has no view to go to.
- The Tasks tab is brought to the front when a dialog's page FIRST comes
  up, and when a dialog already shown is asked for again; not when its
  view is merely come back to. The tab's busy icon is on while the page
  shown is a dialog's.
- Closing a view takes its dialog with it, and an edit that runs in it is
  left first (13.3, point 1).
- Nothing changes for one view, or for a dialog nobody owns: it shows over
  everything, as every dialog did.

### 13.2 What is built

**The task view** (`src/Gui/TaskView/TaskView.{h,cpp}`).

- `showsFor(info, view)` is the rule: a dialog's own view; every view for
  a dialog nobody owns; and every view that is in the edit the dialog's
  view started (13.3, point 2). `infoFor(view)` picks by it, nobody's
  first. The view asked about is the main window's active one, never
  `TaskOwner::current()`: a client's request must not change what the
  desktop's task view shows.
- `showPage` puts a page up; `syncActivation` tells the dialogs;
  `setShownTaskInfo` is the two. They are apart because `showDialog` shows
  the page, THEN calls `open()`, then activates: opened, then activated.
- **The watchers are on their page only while it is the one shown**, taken
  off and put back as it is left and returned to, exactly as they were
  taken out of the one shared panel while a dialog was up. Every "is a
  dialog open?" guard that meant "are the watchers showing?" asks
  `watchersShown()` now -- seven of them; a selection change with a dialog
  open for ANOTHER view updates the watchers, which it did not before.
- The hint is a `TaskBox` named `taskPanelElsewhere` at the top of the
  watchers' panel, made anew whenever the shown page or the set of dialogs
  changes, hidden while it has nothing to say.
- `TaskDialog::activate()` / `deactivate()`, virtual and empty. A dialog
  is active while its page is the one shown; each `activate()` is ended
  by a `deactivate()`, the last one before `closed()`. They are told by
  the dialogs' own pointers, looked up again each time, because what a
  dialog does when told may close one. Nothing is done to the selection
  gate: sec 12 made it the view's.
- **Contextual panels** are kept with the document they were added for
  (`addContextualPanel(panel, doc)` no longer ignores `doc`): shown at the
  top of whatever page is shown while a view of that document is the
  active one, and parked in a hidden widget otherwise. A null document is
  every view's, as before.
- Three signals for whatever holds the task view: `dialogShown()`,
  `shownDialogChanged(bool)`, `shownDialogClosed()`. The combo view's tab
  switching and busy icon hang off them
  (`ComboView::onDialogShown` and its two siblings) where they hung off
  `showDialog` / `closedDialog`. `Control().signalDialogActivated(owner)`
  is sent with them; nothing listens yet (M5).

**Closing** (sec 5.4).

- `Application::signalCloseView` and `Application::viewClosed()`, as
  upstream has them, called from `MDIView::closeEvent` once the close is
  accepted and before anything is taken down -- and from
  `ViewArea::closeEvent` for every view in its cells, which go with their
  container without being closed one by one. `signalDetachView` was there
  already and is not this: it is sent only for a document's views and
  after the document has let go.
- `TaskDialog` has upstream's three switches, off by default, each with
  its `autoClosedOn...()`: on reset edit (the dialog of the EDIT's view,
  `Gui::Document::editingViewer()`, heard on `signalResetEdit`), on
  deleted document (the document the dialog names, or its view's), on
  closed view. The deleted document is heard on the App signal, connected
  AHEAD of every other listener (`fastsignals::at_front`): the GUI's own
  listener closes the document's views before it says anything itself
  (`Gui::Document::beforeDelete`), and by then a view closed with a
  dialog has rejected it -- which is what the first build did.
- `TaskView::ownerClosed(owner)` closes what a going view owns, inside
  that view's own `ViewerScope`. A client's mirror calls it from its
  destructor, after it has left its edit
  (`Control().ownerClosed`). A view that died with no word at all leaves
  an owner that is no longer valid; the next activation finds it and
  closes its dialog from a zero timer, because while only one dialog may
  be open a stranded one locks the task view for good.

**Control** (`src/Gui/Control.{h,cpp}`).

- One `ActiveDialog` and the file-static `_dialogSelectionView` are gone:
  a record per open dialog -- the dialog, the view that took a selection
  instance for it, whether it has been handed to the task view. `dialogOf`
  reads the records; `blockerOf(owner)` says which open dialog keeps one
  from being shown for a view; `mayShowDialog()` is that question public,
  for a caller that must know before it builds the dialog.
- `accept` / `reject` / `closeDialog`, in every form, name the dialog to
  the task view. Left to itself the task view takes the page it shows,
  which with a dialog in another view is not the one asked about.
- `exclusive()` reads the hidden `TaskView/TaskPanelAllowConcurrent`
  (default off, no preference page): the test-only switch of sec 8. With
  it on, one dialog per view; a dialog nobody owns still shares the task
  view with no other.
- `Gui::Document::setEdit` names the document on the dialog of the edit's
  view.

**Python** (`src/Gui/TaskView/TaskDialogPython.{h,cpp}`).

- `Gui.Control.showDialog()` returns the task dialog, as upstream's does.
- The task dialog has `setDocumentName`, and `setAutoCloseOnResetEdit`,
  `...OnDeletedDocument`, `...OnClosedView` with their `is...` forms.
- A panel's `autoClosedOnTransactionChange`, `autoClosedOnResetEdit`,
  `autoClosedOnDeletedDocument` and `autoClosedOnClosedView` are called,
  and its `panelActivated` / `panelDeactivated` (13.3, point 3).

### 13.3 Where it departs from the design, to be confirmed

1. **Closing an edit's view LEAVES the edit; it does not reject its
   panel.** Sec 5.4 and the test of M2 say "closing the editing view
   rejects the panel". In this fork a sketch's `reject()` is
   `cancelEditing()`: it undoes everything done since the sketch was
   entered. Closing a view would have thrown the sketch's work away with
   no question asked. So the edit is left, as it is when its document is
   closed (`Document::canClose` has always done `_resetEdit()` there):
   what was done is kept. Leaving an edit closes its panel as it always
   has. A dialog the view still owns after that -- one that is no edit's
   -- has nobody left to answer it and IS rejected, unless it asked to be
   told instead (`setAutoCloseOnClosedView`). Upstream's switch decides
   whether a dialog closes with its view; here it decides only how.
2. **An edit that every view of its document shares shows its panel in
   every one of them.** With `PerViewEdit` off (the default) a sketch
   entered in one cell is drawn in from any view of the document; hiding
   its panel when the user clicked into the next cell would take the
   panel away from an edit they are still in. The rule is the one sec 6
   already has for the browser (`EditingRoot::isShared()`), asked for the
   desktop. A view of ANOTHER document shows the watchers and the hint.
   With `PerViewEdit` on, the panel is its one view's.
3. **A Python panel is told by `panelActivated()` /
   `panelDeactivated()`**, not `activate()` / `deactivate()`. Panels
   already have methods of those names that mean something of their own:
   Assembly's five `deactivate()` tear the tool down, and would have been
   called the first time their view was left; FEM's base task panel has
   both. Upstream does not pass the two on to Python at all.
4. A served client's dialog is active from `open()` to `closed()`: its
   view is the only one its client has, and the desktop's task view never
   shows its page.
5. A contextual panel is put at the TOP of the page when it is added.
   Before, it was appended on being added and moved to the top the first
   time the page changed.

### 13.4 Found on the way

- **A crash, on the tree before: closing the view a sketch is being
  edited in.** Not this milestone's doing, but exactly on its path, and
  the reason for point 1 above being built at `closeEvent`. The viewer's
  destructor gave the edited geometry back and left the document's
  session bound to it (`Gui::Document::_editingViewer`, and the sketch's
  own copy); sec 12 then gave the view a selection instance of its own,
  which goes with the view and, going, tells its observers to read the
  selection again -- the sketch among them, through a viewer that no
  longer existed (`ViewProviderSketch::constraintPreselectInViews`).
  Before sec 12 the same close left a dangling edit and no crash was
  seen. Fixed twice over (`9469facb2d`): the edit is left while the view
  is whole (`Application::viewClosed`), and a desktop viewer destroyed without
  being closed leaves its edit in its own destructor, as a client's
  mirror already did.
- **Draft's `setDocumentName` on None** (sec 11.1) is gone with
  `showDialog` returning the dialog: `gui_selectplane.py` and
  `gui_scale.py` work as written, and `DraftGui.py` lost its guard. Every
  Draft, BIM and Assembly panel that asked to close with its document
  now does.
- A Python panel's `autoClosedOnTransactionChange()` was never called.
  Assembly's joint panel has one (it tears the joint tool down on an
  undo); it runs now.
- `Control().showDialog(nullptr)` with no dialog open walked into the
  null pointer; it warns and returns.
- The main window with no combo view (sec 11.1) now records its dialog as
  open like any other.
- `tests/gui`'s runner isolates the configuration and the cache but not
  `XDG_DATA_HOME`: the crash above wrote its log into the real
  `~/.local/share/FreeCAD`. Not changed.

### 13.5 The audit of pages that are alive and hidden

Sec 5.1 asks for it: a page whose view is not active is alive and hidden,
and nothing in it may assume it is visible.

- No task box in `src/Mod` overrides `showEvent` or `hideEvent` in C++.
- In Python two widgets near a panel do. CAM's `IconTabWidget`
  (`Path/Op/Gui/Base.py`) installs event filters on its ancestors when
  shown and removes them when hidden, and schedules a relabel: symmetric,
  and about its own layout. FEM's `extract_link_view` is a popup that
  says `close` when hidden. Neither starts or stops work a hidden page
  would break.
- `Gui::PropertyLinkEditor` attaches its selection observer on show and
  detaches it on hide; it is a dialog of its own, not a page's child.
- What a hidden page does lose is its PICTURES in the browser:
  `Fw::PanelMirror::grabPicture` returns nothing for a widget that is not
  visible, and a client-owned dialog's page is now never the one the
  desktop shows. The models -- every widget the mirror walks -- do not
  depend on it (`visible` is read as "not explicitly hidden"), and a
  picture was already missing whenever the desktop's Tasks tab was not
  the one in front. For M5.
- A page's widgets are used while its view is not active by exactly one
  path today: a keyed `Control().accept(owner)` and its siblings. The
  rest of sec 12.5's second point stands for view mode.

### 13.6 Measured

`tests/gui/task-panel-combo.py` (`GuiTaskPanelCombo_tests_run`): document
A with two views, a second and a third document, the Assembly's solver
panel for the contextual one. 66 checks. Scored on the tree before
(`c108db0c04`) twice, because that tree does not survive the test:

| | before | after |
|---|---|---|
| as written | 17 of the first 31, then SIGSEGV on closing the editing view | 66 of 66 |
| with that one step left out (`GT_NO_EDIT_VIEW_CLOSE`) | 28 of 61 | -- |

What moved, by what the user would see:

| | before | after |
|---|---|---|
| a1's panel, with a2 active | still shown | the watchers, and "Go to <a1>" |
| the Tasks tab's busy icon, with a2 active | on | off |
| back in a1 | the panel | the same panel, what was typed still in it |
| the tab the user switched to, on coming back | kept | kept |
| a sketch edited in a1 with `PerViewEdit`, a2 active | its panel | the watchers and the hint |
| the same with `PerViewEdit` off, a2 active | its panel | its panel |
| ... and another document's view active | its panel | the watchers and the hint |
| closing the view a sketch is edited in | SIGSEGV | the edit is left, the line drawn in it kept |
| closing a view that owns a plain panel | the panel stays, for nobody | rejected |
| a panel that asked to close with its document | `showDialog` gave None to ask on | told, not rejected |
| a second panel for another view, test switch on | refused | shown, each for its own view |
| the Assembly's solver panel, another document's view active | shown | not shown |
| a panel told it is activated / deactivated | never | once per visit, each ended |

The readings are the real ones: the stack's current page, the tab bar's
icon and index, the widgets' own visibility up to the task view, and the
hint's buttons clicked. Two of the test's helpers were tightened AFTER the
before runs -- "shown" now means nothing between the widget and the task
view is hidden, and a hint line awaiting deletion is not counted -- so the
before column was read by the looser forms; no check that passed there
depends on the difference as far as reading them tells.

`tests/gui/edit-view-closed.py` (`GuiEditViewClosed_tests_run`): the crash
of 13.4 on its own -- a sketch and an edit with no dialog, `PerViewEdit`
on and off, and a view that is not the edit's closed beside it. 21 of 21.
It was written after the fix and so was never run on the tree before; the
crash it guards is the one the first test measured there, by the same
close.

The three tests nearest: `GuiTaskPanelOwner` 34 of 34, `GuiDraftPanelOwner`
2 of 2, `GuiSelectionPerView` 26 of 26.

The suites on it (`03c509ac9a`): Python 3379 OK; ctest 957 of 957, 966
entries, 3806 s serial; the GUI gate 70 OK. Rows in `docs/Testing.md`,
with what a fresh user home does to the gate.

### 13.7 What it does not do

- **A view is closed without a question.** An edit in it is left and a
  plain panel it owns is rejected, with no "this view has a task panel
  open" first. Closing the last view of a document still asks what it
  always asked.
- **The keyed forms go by owner alone.** With `PerViewEdit` off a second
  view of the document SHOWS the sketch's panel, but
  `Control().activeDialog(owner)` asked for that view answers none: the
  dialog is still the first view's. The argument-less form answers it, as
  it answers any dialog while only one may be open.
- **A dialog opened for a view that is not the active one** -- a deferred
  show that carried its owner, a client's -- raises nothing: the Tasks tab
  comes to the front when its page first does.
- The five deferred closes and two queued accepts of sec 11.1 still act
  on "the" dialog.
- The hint's three strings are new and untranslated.
- With the test switch on and no combo view, the standalone task dock is
  still deleted with the first dialog it was made for.
- Everything of sec 5.2 to 6: the panel in its view, the dock overlays,
  the browser. *(The panel in its view is sec 14.)*

## 14. Milestone 3: the panel in its view (2026-10-07, `0c5cb84070`)

View mode of sec 5.2, the switch of sec 5.5, and the scope sec 12.5 said
view mode would need. On the stack of sec 11.4, the selection of sec 12
and the activation of sec 13.

### 14.1 What the user sees

- A preference, "Task panels in their views" (`View/TaskPanelInView`, off
  by default; Preferences > Display > UI, in the Views group).
  The same switch is a button on the title bar of the dock that holds the
  task view -- pressed in while the panels are in their views -- and the
  button in the header of a panel that is in its view, which sends the
  panels back.
- With it on, a dialog's page stands INSIDE the view it was opened for,
  over the picture, along the left edge: a slim header with the dialog's
  title, a button that folds the panel down to that header and the
  send-back button, then the dialog's buttons and boxes as the Tasks tab
  shows them. It is as wide as the panel asks for within a third of the
  view, and as tall as the panel needs -- a two-line panel does not cover
  the height of the view.
- It stays there whichever view is active. Two cells of a split, each with
  a panel, show both at once.
- The header is a grip: dragged across, the panel settles on the nearer
  side of its view. The side, and whether it is folded, are remembered per
  kind of view. A view too small for a panel keeps the header alone.
- The Tasks tab keeps the watchers, with no busy icon. Its "Task panel in
  another view" box names only the panels that are out of sight -- in a
  tab behind the one shown -- since one that is in sight says where it is
  by being there.
- A click into a panel makes its view the active one, whatever is clicked.
  Enter and Escape act on the panel they are pressed in.
- Switching moves the pages and nothing else: no dialog is closed, an edit
  stays on, what was typed and where the keyboard was are kept, and no
  panel is told anything.
- A dialog nobody owns stays in the Tasks tab, over everything, as before.

### 14.2 What is built

**The host** (`src/Gui/TaskView/TaskPanelHost.{h,cpp}`).

- `TaskPanelHost` is a plain widget, the child of "the widget the view
  fills": the view's `ViewAreaCell` when it is in one, else the `MDIView`
  itself -- one code for both, which is sec 5.2's "by the same code". It
  holds a header and the dialog's `TaskPage`, and lays itself out in its
  place: an event filter on the place answers its resizes, and one on the
  page's panel answers a box being folded or a widget shown, since the
  host's height is the panel's.
- It stands clear of the cell's own chrome: 4 px in from the border, 20
  below the top (the menu button, the top right zone) and 18 above the
  bottom (the bottom left zone).
- Width is the panel's size hint between 240 px and a third of the place.
  Under 360 px of width or 160 of height the place is "too small" and the
  page is hidden, the header left.
- **It follows its view.** A view that goes into a cell, to another, or
  out of one is heard by its `ParentChange`, and the host stands in the
  new place once the move is over (a queued call: the event arrives from
  inside it).
- **A page outlives its host.** A dialog owns the widgets in its page and
  deletes them itself, so a page must never die as somebody's child before
  its dialog has. The task view takes the page out before it lets a host
  go (`release()`: hidden, never shown again, deleted from the event loop
  -- it may be asked from one of the host's own buttons). And a host that
  is destroyed WITH its place -- a cell collapsed, a view deleted with no
  close -- hands the page back from its own destructor
  (`TaskView::hostGone`); the task view then puts it in a host in the
  view's new place, or closes the dialog of a view that is gone.
- `ViewAreaCell::taskHost()` answers the host laid in a cell.

**The task view** (`src/Gui/TaskView/TaskView.{h,cpp}`).

- `TaskInfo::host` says where a page is: null in the stack. `placePage`
  decides: in its view when the preference is on and the owner is a view
  of this window; in the stack otherwise -- always for a dialog nobody
  owns and for a served client's, whose view has no widget.
  `applyHosting()` is the switch: every page to its place in one pass.
- **Activation follows the view, not the place** (sec 4.3). `syncActivation`
  asks which dialog the active view calls for (`infoFor`), where it asked
  which page the stack shows; in combo mode the two were always the same.
  So the switch activates and deactivates nothing.
- `showPage` leaves a hosted page alone: the watchers keep the stack.
  `isEmpty()` counts the stack's pages only, so the overlaid Tasks dock is
  not brought up for a panel that is in its view (sec 5.3's point, taken
  now: it is what "the Tasks tab holds only the watchers" means).
- `pageKeyPress(page, key)` is what `keyPressEvent` was, for a named page:
  the host forwards its keys to it, so Enter and Escape find the buttons
  of THAT dialog. `acceptEditingKeys` is the Shift+keypad work-around of
  `event()`, for the host as well.
- The argument-less `accept()` / `reject()` / `removeDialog()` act on the
  page shown, else the active view's dialog, else the only one.

**A panel used while its view is not the active one**
(`TaskPageEventScope`, opened in `GUIApplication::dispatchEvent`). Sec
12.5 left this open: a hosted panel's widgets are in reach while another
view is active, and their slots ran as the ACTIVE view -- selected in its
selection, answered `TaskOwner::current()` with it.

- For an event delivered to anything in a host -- a widget, or an object
  one of them owns, a `QTimer` say -- whose view is not the active one, a
  `ViewerScope` is open on the host's view for the delivery (a
  `SelectionScope` on a view that is no 3D view and selects on its own).
  So what the panel's code does it does for its own view.
- A mouse press makes the view the active one first, and then needs no
  scope: "a click into it makes its view the active one" (sec 5.2) is
  done here, by name, and does not depend on the widget clicked taking
  the keyboard focus.
- Only for events that can run a panel's code -- input, focus, hover,
  drops, queued calls, timers; never for painting and layout, which are
  most of what a widget is sent. And nothing at all while no view hosts a
  page: one test of a counter per event, the counter atomic because every
  thread with an event loop delivers through the application object.

**The switch.**

- `ViewParams::TaskPanelInView`, with a change handler that moves the
  pages and sends `Control().signalHostChanged()`. The preference is the
  truth; the two buttons only set it.
- `OverlayManager` has one action more, `OBTN TaskHost`, checkable, on the
  title bar of the dock whose content holds a `TaskView`
  (`setupTitleBar(dock, content)`: the dock window manager makes the title
  bar before the dock has its widget). Icon `qss:overlay/taskhost.svg`.
- `Control::showDialog` no longer brings a hidden combo view up for a
  dialog whose page went into its view.
- `Fw::PanelMirror`, started with a dialog already open, looks for the
  button box in the main window when the task view has none: the page may
  be in a view.

### 14.3 Where it departs from the design

1. **The cell does not lay the host out; the host does.** Sec 5.2 gives
   `ViewAreaCell` a `taskHost()` it creates and lays out in `resizeEvent`.
   The host instead watches whatever it stands in, so a view with no cell
   needs no second implementation and the cell knows nothing of task
   panels beyond the accessor.
2. **As tall as the panel, not as the view.** Sec 5.2 says "full height
   less a margin". The first picture of it settled that: a one-line panel
   as a strip down the whole view. A dialog that asks for all the room
   (`needsFullSpace()`) still gets the full height.
3. **Opaque.** Sec 5.2 would borrow the dock overlay's translucent look.
   That look is not a style sheet to borrow but a mode
   (`OverlayTabWidget::_setOverlayMode`) that walks a dock's widgets and
   switches attributes on each; it belongs with the overlays, in M4.
4. **Left or right, nothing else.** "Dragged along the edges of its cell"
   is built as the two sides. No top or bottom, no free place, and no
   grip to change the width.
5. **One icon, shown pressed, not an icon pair** for the title bar button.
6. **The line on the watchers' page** in view mode -- not said in the
   design -- is for the panels out of sight only.
7. **The scope is for view-hosted pages only.** A page in the Tasks tab
   shown for a view that merely joined its edit (13.3, point 2) is still
   handled as the active view; there the session's selection is one for
   both, which is what mattered.

### 14.4 Found on the way

- **A view made the active window was not the active cell.** Its own
  subject and its own commit (`f86654a5b7`, `docs/SplitViews.md` sec 21):
  `MainWindow::setActiveWindow` recorded the view and left the view
  area's active cell, and the keyboard, where they were. Found because
  "maximize view cell" maximized the wrong cell under the test, it turned
  out to be why, after "create new view", the first view cannot be
  clicked back into.
- **A mouse event sent through Qt to a 3D view is not a click.** The
  viewer does not take it as it takes the pointer's: the event goes up to
  the `QMdiSubWindow`, which takes the keyboard and hands it back to
  whatever last had it in the tab. `QTest.mouseClick` on a view therefore
  re-activates the cell that already had the keyboard, whichever was
  clicked. The tests click a view with the real pointer (XTEST). On
  widgets -- a line edit, a button -- `QTest` is fine.
- **A view in a tab behind the shown one is visible to Qt**
  (`isVisible()` true): `QMdiArea` stacks its sub windows, it does not
  hide them. "In sight" is asked of the tabs (`inSight`, `TaskView.cpp`).
- **`Std_ViewUndock` does nothing for a view in a cell**, and it left the
  test's plain view docked as well, with a dialog open for it (why was
  not chased); that step is skipped there. The move that IS tested is a
  plain view split, which wraps it into a cell: its host goes with it.
- **The header named a box that was not shown** (`58968bd13b`). Titled by
  the dialog's first box, a sketch being edited was headed "Tool
  Parameters" -- its tool's box, hidden until a tool has something to ask
  -- above a panel that began with "Sketch Edit". Seen in the first
  picture taken of a sketch; the title is now the first box's that is not
  hidden.
- In Python, a widget fetched through a wrapper that is then dropped --
  `host_of(view).findChild(...)` -- is "already deleted" to the binding:
  the wrapper of a parent that goes takes its children's with it. The
  test keeps the hosts it looks at for the length of a step.

### 14.5 Measured

`tests/gui/task-panel-in-view.py` (`GuiTaskPanelInView_tests_run`):
document A with a box, a body, a sketch and a pad in two cells, a second
document in a tab of its own, a third in a view outside any view area. 89
checks.

| | before (`26b042b7f1`) | after |
|---|---|---|
| as it stood when scored | 14 of the first 36, then the script stops | -- |
| as it is | -- | 89 of 89, one step skipped |

Scored on the tree before, the script was two checks shorter in the part
that tree reaches (the host's height, the click back into the other view)
and its later steps were reworked afterwards, one check added to them
(the header's title); the 14 that passed there are unchanged, name for
name. It stops because on that tree Enter in one
view's panel accepts the OTHER view's dialog, and the script then reaches
for a widget that is gone.

What moved, by what the user would see:

| | before | after |
|---|---|---|
| the preference on, a panel open for a1 | in the Tasks tab | in a1's cell, as it was left |
| the Tasks tab then | the panel, busy icon on | the watchers, no icon |
| a2 active | the panel hidden | still shown in a1 |
| a click into the panel, a2 active | -- | a1 is the active view |
| a button of a1's panel used with a2 active, by key | selects in a2 | selects in a1 |
| a timer of a1's panel firing with a2 active | selects in a2 | selects in a1 |
| a panel in each cell (test switch) | one shown at a time | both |
| Enter in a1's panel, a2 active | accepts a2's | accepts a1's |
| the host's button, the title bar button | no such buttons | the preference flips, the page moves, the dialog is the same |
| a sketch edited in a1, the switch flipped both ways | -- | still edited, panel in the cell, then the tab, then the cell |
| a length typed into a pad's panel in the cell | -- | the pad's length |
| a view outside any view area | -- | hosts in itself; split, the host is in the cell |
| the neighbour cell maximized | -- | the panel goes with its cell, and comes back |
| a view, and a document, closed under a panel in it | -- | the dialog closed, no host left |

The readings are the real ones: the host's parent and geometry, the
stack's current page, widgets' own visibility, keys and clicks through
`QTest` on the panel's widgets and the real pointer on a view.

NOT measured: the unified canvas (`View/UnifiedCanvas`) with a host in a
cell -- sec 16.1 of `docs/SplitViews.md` measured that a cell's child
widgets draw above the canvas, and nothing here was run on it; a view
kind other than a 3D view as the owner; a dark style sheet.

The suites, on the tree `f86654a5b7` and `0c5cb84070` record: Python 3379
OK; ctest 958 of 959, 968 entries, 3878 s serial -- the one is
`ExpressionImageBudgetTest.runawayBytecodeLoopIsStopped`, a sandbox case
that failed inside the full run in the words it failed in once before
(2026-10-05) and passes alone, 3 runs of 3: not this change's as far as
anything shows, and not chased; the GUI gate 70 OK, run with the panels
in the combo view -- the panel mirror was not run with a panel in its
view. Rows in `docs/Testing.md`. `f86654a5b7` was not built on its own:
the two were built and tested together. `58968bd13b`, after them, was
given the nine GUI entries nearest it (the task panel tests, the active
cell, the selection per view, the edit per view and its two neighbours),
9 of 9, and no full run.

### 14.6 What it does not do

- **Keep clear of the dock overlays** (M4), and look like them.
- **The title bar button while the combo view is overlaid.** An overlaid
  dock's title bar is the overlay's own, shared by the docks in it, and
  does not carry the button. The preference and a host's button do. For
  M4, with the overlays.
- **A served client's panel** is where it was: in the stack, never shown
  on the desktop (M5).
- **Resize.** The width is the rule of 14.2 and no grip changes it. In a
  narrow view that rule is hard on the picture: in the test session's
  cells, some 425 px wide, a panel at its least width of 240 px covers
  more than half the cell and still scrolls sideways.
- **Move the picture aside.** The panel is over the view, and "fit all"
  centres in the whole of it: part of what is fitted is under the panel.
- **Popups a panel opens with no parent in it** -- a menu built
  parentless, a dialog of its own -- are not in the host, and their
  events are the active view's. A combo box's list has the box for its
  parent and is found by the same walk (Qt's structure; not tested).
- **What sec 12.5 still lists**: observers a dialog keeps outside its
  widgets follow the active view; a Python panel cannot opt out of its
  own selection; deferred calls made by a panel's code run with no scope
  once the event that made them is over.
- The new strings -- the host's tool tips, the title bar button's, the
  preference's -- are untranslated.

## 15. Two modes for the panel in its view (ordered 2026-10-07; DESIGN, not built)

### 15.1 The order, in the user's words

Four messages, the later ones answering what the earlier left open.

> I want the panel to also have two mode as the global one. Overlay mode or
> side by side mode. Side mode means the panel is at a subcell attached to
> any side of its view. Overlay means it is inside the view resizable and
> translucent. Behave and look basically the same as global overlay panel.

> The panel will have two state. One if it is attached to global or not,
> the setting act on the panel who own the button and all later views, but
> not other existing panels. The other state is when attach to view whether
> overlay or in subcell and which side this state persist into a view
> property and apply to later created view.

> To avoid user confusion, let's make setting in preference (state 1 only)
> apply to all later created panels, with an option to apply to all current
> view. The button on title bar act on the own panel/view, apply to state 1
> and 2. 4, for all types of view. 5, real cell split out from the hosting
> view which move to inner cell. 6, not applicable, always show in edit

> 1, state 1 is persist in view also. next panel follow what's persist in
> the view. 2 yes

Read together:

- **State 1, where**: in the combo view or in its view. A state of each
  VIEW, kept with the view. The preference is state 1 only: it is what a
  panel created later gets in a view that holds nothing of its own, and a
  button beside it writes it into every current view.
- **State 2, how, while in its view**: overlay or side cell, and which of
  the four sides. Kept with the view, and what views created later start
  from.
- **The title bar buttons** act on their own panel and view only, on both
  states. The combo view's button acts on the panel the Tasks tab shows.
- **Every kind of view** carries the states, not 3D views alone.
- **The side cell is a real cell**, split out of the hosting view's, the
  view going to the inner one.
- **No auto-hide, edit-show or task-show** for the overlay in a view: it is
  shown for as long as its edit or dialog is on.

### 15.2 What it overrules

- Sec 10 assumption 2 and sec 5.5's last line, "one mode for all panels".
  `TaskView::applyHosting()` moving EVERY page when `View/TaskPanelInView`
  changes (sec 14.2) goes: the preference stops moving anything.
- Sec 14.3's departures 3 and 4 and sec 14.6's "resize": opaque becomes
  translucent, left-or-right becomes any side, and both modes resize.
- The user parameters `TaskView/Host/<view type>` (side, folded): the view
  holds them now.
- Today's look of `TaskPanelHost` -- a plain widget with a header of its
  own -- in favour of the dock overlay's.

### 15.3 The state

Four properties on `Gui::MDIView`, so that every kind of view has them, in
a group of their own as the `Render_*` properties of a 3D view are:

| property | values | a view that holds none |
|---|---|---|
| `Task_Place` | `ComboView`, `InView`, empty | the preference |
| `Task_Mode` | `Overlay`, `Side` | what was last chosen in any view |
| `Task_Side` | `Left`, `Right`, `Top`, `Bottom` | what was last chosen in any view |
| `Task_Size` | pixels across, 0 = the panel's own hint | 0 |

- **A property is made when the user chooses in that view**, by a button
  of its panel's title bar, and not before. Made with the view instead,
  the four would mark every document modified for having a view opened,
  and ride in every file. So "views created later" is realised as: a view
  that holds nothing follows what is general when its next panel opens --
  which is also so for a view that was there before the choice and has
  not been chosen in. (Mine; the order says "later created view".)
- `Task_Place` empty follows the preference as it is at the moment a panel
  is opened. So a view the user never chose in keeps following the
  preference, and one he did keeps what he chose -- "next panel follow
  what's persist in the view".
- **A panel that is open stays where it was put** until its own view's
  property changes: not moved by the preference, nor by what is chosen in
  another view. The task view remembers the place per dialog for that.
- **The option beside the preference**, `View/TaskPanelInViewAll` ("... and
  the panels that are open"): while it is on, a change of the preference --
  and turning the option on -- makes every view follow the preference at
  once. The views' own places are given up (emptied) and the open panels
  move.
- Changing a property moves the panel of that view and no other, also
  when it is set from Python. Nothing is closed, no dialog is told
  anything: the page moves as it does today.
- "What was last chosen in any view" for state 2 is a user parameter
  (`TaskView/Host`: `Mode`, `Side`), so it is remembered across runs,
  written whenever a button changes a view's mode or side. It has no
  entry on the preference page -- the user said the preference is state 1
  only.

### 15.4 Side mode: the panel pair

`ViewArea` is a tree of splitters whose leaves are `ViewAreaCell`s, each
holding exactly one view. Much of it counts and walks those cells: the
last cell closing its container, the choice of the active cell, the join
gesture, the placement of new views, the layout string saved with the
document.

A panel cell that WERE a `ViewAreaCell` with no view would have to be
taught to every one of them. So it is not one. The cell's own slot in its
splitter is taken by a small splitter -- the PAIR -- that holds the panel
cell and the view's cell, in the order and direction the side asks for;
the view "moves to the inner cell". The panel cell is a widget of its own
class.

- Everything that enumerates cells does not see it. It cannot be joined
  into, split, made active, or picked for a new view.
- The pair stands in exactly one slot, so the enclosing splitters keep
  their sizes: the saved layout, the proportions a maximize puts back, and
  the unified canvas's reading of cell geometry are untouched. The layout
  string writes a pair as its cell and nothing else; a panel is there only
  while a dialog is, and a document is not reopened into one.
- Taking the panel cell away is the un-nesting `collapseCell` already does
  for a splitter left with one child.
- What must learn of the pair: `collapseCell` and `childViewGone` (the
  panel cell goes before its view's cell does), `toggleMaximizeCell` (its
  walk hides every sibling on the way up, and the panel must stay with a
  maximized view), `splitCell` and `joinTargetFor` (a split of the view's
  cell puts the new cell OUTSIDE the pair, so the panel stays beside its
  own view; a join sees a pair as the one cell it stands for).
- The handle between the two is the view area's own, and resizes the
  panel. Its position is `Task_Size`.
- A click into the panel cell makes its view the active one, as now
  (`TaskPageEventScope`).

### 15.5 Overlay mode: what of the dock overlay is used

Looked at first, as ordered: can `OverlayTabWidget` hold a task page in a
cell? No. It is four fixed instances (`_LeftOverlay` ...), each built round
`QDockWidget`s, placed by the geometry of the main window's MDI area, and
driven by `OverlayManager`'s application-wide event filter, which names
them. Twenty-five hundred lines that assume all of that.

What IS used, each as it stands:

- **The style sheet**, `OverlayManager::getStyleSheet()` -- the
  `overlay:*.qss` the user picked, so the panel follows the same theme
  setting as the docks.
- **The switch of attributes**, `OverlayTabWidget::_setOverlayMode`: the
  walk that makes each widget frameless and translucent. It SKIPS a
  `TaskBox` and a dialog, which is why a task panel in the overlaid combo
  view has clear ground and boxes that keep their own background. The
  same walk on the host gives the same picture. It is a protected static
  today and gets a public door.
- **The outline effect**, `OverlayGraphicsEffect`, which keeps text
  readable over the picture.
- **The title bar's parts**, `prepareTitleWidget` and `createTitleButton`
  (public statics): the row of buttons made from actions, under the object
  name the style sheet styles. `OverlayTitleBar` itself drags DOCKS and is
  not used; the host's header keeps its own drag, to any of four sides now.
- **The size grip**, `OverlaySizeGrip`, which only reports where it is
  dragged to.

What is not taken, by the order: the auto modes and their menu, the hint
strip that brings a hidden overlay back, the tabs.

**Mouse pass-through** is the dock overlay's hardest part -- the filter
tests the alpha of a grabbed picture under the cursor and hands a click on
nothing to the widget beneath, holding the mouse for the drag that
follows. It is written for the four docks and the MDI area. The host needs
the same answer and a smaller question, since what lies beneath it is its
own view; it is its own step (15.7, M4), after the look.

### 15.6 Kept with the view

- A 3D view writes its properties into `GuiDocument.xml` (`<View3D>`), so
  the four ride along with no new code.
- No other view writes anything there: a TechDraw page or a spreadsheet is
  made again from its object. For "every kind of view" to mean kept, those
  need an entry of their own, keyed as the layout string keys them
  (`O:<object>`). A file-format addition, to be checked against older
  builds before it is made.
- A view with no document keeps its state for the session.

### 15.7 Milestones

Each with a GUI test scored on the tree before, as sec 9 asks.

- **M1. The state.** The properties, a panel placed by its view's, the
  preference as the seed, "apply to the current views", the buttons acting
  on their own view. No new look. Rests on the order alone.
- **M2. Side mode.** The pair, four sides, the handle, `Task_Size`.
- **M3. Overlay mode's look.** Style sheet, attribute walk, effect, title
  bar, grip, four sides.
- **M4. Pass-through, and the dock overlays.** The click on nothing; and
  sec 5.3, still owed: an overlay panel in a view standing clear of an
  overlaid dock on the same edge. Side mode needs none of it.
- **M5. Kept for views that are not 3D views** (15.6).

### 15.8 Put to the user, and answered (2026-10-07)

> 1 how can a view be outside viewarea. 2 full height. 3 last chosen needs
> a setting to remember across runs, isn't it? 4 remove

1. **A view outside any view area, in side mode.** The pair needs a
   splitter, and such a view has none. It is not the usual case: the
   preference "Tile views inside one tab" (`View/UseViewArea`, on by
   default) turned off, a view opened as a tab of its own (the Alt
   inversion of `docs/ViewPlacement.md`, or a document saved before
   layouts restored tab by tab), or a view undocked. The placement policy
   already promotes such a bare tab to a view area when a split is asked
   of it (`ViewArea::wrap`, as `splitActiveView` does), and a side panel
   is a split: so it is wrapped. A floating view, which `wrap` does not
   take, shows the overlay. (Mine, on the user's question; not objected
   to.)
2. **The overlay is the full height of its side**, as a dock overlay is.
   Until M4 nothing under the panel's clear ground can be clicked.
3. **State 2's start for later views** is "what was last chosen": a user
   parameter, so it is remembered across runs, with no entry on the
   preference page (15.3).
4. **The fold-to-header button** of sec 14 is removed.

### 15.9 Milestone 1, built (2026-10-07, `c969119033`)

The state, with no new look: the panel in its view is still sec 14's host.

**What the user sees.**

- "Task panels in their views" on the preference page now reads as it
  acts: it is for the panels opened from now on. A panel that is open
  stays where it is.
- Under it, "... and the panels that are open": ticked, every panel goes
  by the preference at once, now and whenever the preference changes, and
  what the views had chosen for themselves is given up.
- The button in the header of a panel in its view sends THAT panel to the
  combo view, and the view remembers: its next panel opens in the combo
  view too, whatever the preference says.
- The button on the combo view's title bar is a push button, not a switch.
  It sends the panel the Tasks tab is showing into its view, and that view
  remembers likewise. A panel comes back by its own header's button.
- A panel dragged to the other side of its view: the view remembers the
  side, and a view that has none of its own opens its next panel on the
  side last chosen anywhere. A panel that is open elsewhere does not move.
- Saved with the document: a view reopened has its place and its side.
- No fold button.

**What is built.**

- `Gui::TaskView::TaskPlacement` (`TaskPanelHost.{h,cpp}`): the four
  properties and what stands in for one that is not there (15.3). The
  texts are `App::PropertyString` -- `ComboView`, `InView`, `Left` ... --
  and not an enumeration, which is kept as an index and reads wrongly the
  day the list changes; anything else in them is taken as "none".
  `Task_Size` is an `App::PropertyInteger`. Group `Task`, added with
  `addDynamicProperty` the first time there is something to keep. A place
  given up is emptied, not removed.
- `TaskInfo::inView`: where a dialog's page belongs, settled when it is
  shown and again only when its own view's place changes. `placePage`
  reads that and not the preference, so `applyHosting` -- which still runs
  when a host is destroyed with its cell -- moves nobody else.
- `TaskView::slotChangedView`, on `Application::signalChangedView`: a
  `Task_*` property of a view changed, by a button or by anything else.
  The place: that dialog's `inView`, then the move. The side: the host is
  told (`sideChanged`). A host takes its side when it is made and again
  only then, which is what keeps an open panel from following another
  view's choice.
- `TaskView::followPlacement` and `TaskPlacement::applyToAll`: the option.
  Every view there is -- the main window's, in a cell or a tab, and the
  ones a document has outside it -- has its own place emptied, and every
  dialog's `inView` is read again.
- `ViewParams::TaskPanelInViewAll`, with the preference's change handler
  no longer moving anything unless it is on.
- `TaskView::sendShownToView` behind `OBTN TaskHost`, which is no longer
  checkable.
- Gone: the collapse button, `setCollapsed`, the header's double click,
  the user parameters `TaskView/Host/<view type>`. `TaskView/Host` itself
  now holds `Mode` and `Side`, the last chosen.

**Where it departs, mine.** The properties are made when the user chooses
in a view, not with the view (15.3). `Task_Mode` and `Task_Size` are kept
and nothing reads them yet; of `Task_Side` the host knows left and right,
and takes the other two for left until a look that can stand there is
built. "Apply to all current views" is an option that stays on, not a
button pressed once: with it on, the preference is the old switch.

**Measured.** `tests/gui/task-panel-place.py`
(`GuiTaskPanelPlace_tests_run`): one document in two cells, two panels
open at once by the test switch.

| | before (`3b38952724`) | after |
|---|---|---|
| as it stood when scored | 29 of the first 44, then the script stops | -- |
| as it is | -- | 49 of 49 |

It stopped on the tree before for a fault of the script's own, a document
reopened under the name of its file; the last five checks -- the place
and the side back with a reopened view, and a panel opened in it going by
them -- were therefore not scored there. What moved:

| | before | after |
|---|---|---|
| the preference turned on, a panel open | the panel moves | it stays |
| the header's button in one view, a panel open in the other | both go to the combo view, the preference is turned off | that one goes, the view holds `ComboView` |
| that view's next panel, the preference on | in the view | in the combo view |
| the title bar button | the preference is turned on, every panel moves | the panel in front goes, the view holds `InView` |
| the preference off, a view holding `InView` | its next panel in the combo view | in the view |
| the side dragged | kept per KIND of view, in the user parameters | the view holds `Right`; last chosen `Right` |
| `Task_Side`, `Task_Place` set from Python | nothing | that view's panel moves |
| a fold button | there | none |

`task-panel-in-view.py` (sec 14.5) runs with the option on, which is what
its steps were written against, and reads what the two buttons did from
the view: 89 checks, as before. The eleven GUI entries nearest the change
pass.

**Not done.** `Task_Mode`, `Task_Size`, top and bottom (M2, M3). A view
that is no 3D view keeps its state for the session only (15.6, M5). The
new strings are untranslated. `pre-commit` is not installed on the box it
was built on: the hook did not run.

### 15.10 Milestone 2, built (2026-10-07, `50560fcf27`)

Side mode: 15.4 as designed, with the points below.

**What the user sees.**

- A menu in the header of a panel in its view: "Over the view" or "Beside
  the view", and "Left", "Right", "Top", "Bottom". It is that view's, and
  the view keeps it.
- Beside the view, the panel is in a cell of its own. The view's picture
  is not covered: the view is given the rest of the room it had, and
  everything else in the view area stays where it was. The border between
  the two is dragged like any border in the view area, and the view keeps
  the size.
- The next panel of that view opens the same way and at the same size. A
  view in which nothing was chosen opens its next panel in the mode and on
  the side last chosen anywhere.
- Splitting the view puts the new view outside the two. Maximizing the
  view keeps its panel beside it. Closing the dialog, putting the panel
  over the view again, or closing the view takes the panel's cell away,
  and the view has its room back.
- A view in a tab of its own is put into a view area when its panel goes
  beside it.

**What is built.**

- `Gui::ViewAreaPanelCell` (`ViewArea.{h,cpp}`), a widget with a layout and
  a ground of its own, and `ViewAreaSplitter::panelPair`.
  `ViewArea::panelCell(cell, side, extent)` makes the pair or moves the
  panel cell to another side of it; `removePanelCell` puts the cell back
  in its slot; `panelCellOf` reads. `slotOf` / `cellOfSlot`, local to the
  file, are "what stands in the tree for this cell" and its inverse, used
  by `splitCell`, `joinTargetFor`, `toggleMaximizeCell` and the handle's
  menu. `collapseCell` removes the panel cell first. `layoutNode` needed
  nothing: a widget that is no cell writes nothing, and a splitter left
  with one entry writes as that entry.
- `TaskPanelHost`: `_mode` and `_side` taken when it is made and in
  `placementChanged()`; `besidePlace()` asks the view area for the panel
  cell, wrapping a view that is in none (`ViewArea::wrap`); in the panel
  cell the host is the one widget of its layout and `place()` has nothing
  to do; `pairMoved()` writes `Task_Size`; `release()` leaves the panel
  cell before it lets it go. The header is no grip while beside the view.
- `ViewAreaCell::taskHost()` looks in the panel cell too.

**Found on the way.**

- **The view area took its own restructuring for the user changing
  cells.** Putting a cell into a pair re-parents it; Qt takes the keyboard
  from the view and the code hands it back; `onFocusChanged` made another
  cell the active one and then this one again. Dialogs were deactivated
  and activated for a panel that only changed its place, and -- when the
  panel was a NEW one, opening straight into a side cell -- the task view
  was asked what to show while that panel's host was still being made,
  and told its stack to show a page the stack does not hold. Qt's warning
  ("QStackedWidget::setCurrentWidget: widget ... not contained in stack")
  brought the report view up, and that is how it was seen: the cells of
  the test had lost 208 pixels of height. Two changes: a flag
  (`pairChanging`) under which `onFocusChanged` stands still, and
  `showPage` takes a page that is in neither place for "none".
- **A view asks for 400 by 300 pixels at the least**
  (`View3DInventor::minimumSizeHint`), and a panel beside it for 120
  more. In a slot too small for both -- two views side by side in a
  1280 pixel window with the docks open have 427 each -- Qt gives the
  pair what the neighbours can spare (27 pixels there) and the view less
  than it asks. The size asked for the panel now leaves the view its
  least, and the pair remembers the neighbours' sizes from before it stood
  (`slotBefore`, `slotAfter`): they get their room back when it goes,
  unless a border was dragged in between.
- **A test that asks the main window for all its widgets poisons its own
  later readings.** `mainWindow.findChildren(QWidget)` makes the binding
  adopt every widget found as a child of the main window's wrapper, for
  good; the widget inside a view among them. Close that view, open
  another whose inner widget is allocated where the old one stood, and
  the binding hands out the dead one's wrapper: "Internal C++ object
  (QGraphicsView) already deleted", one run in three. A garbage
  collection does not clear it. `QApplication.allWidgets()` adopts
  nothing. `docs/Testing.md`, with the other GUI test traps.

**Where it departs, mine.**

- The menu is a plain tool button with a menu, in the host's own header.
  The dock overlay's title bar and buttons are M3's.
- Over the view, top and bottom are still taken for the left (M3).
- One number, `Task_Size`, for a panel beside and a panel above. Since
  milestone 3 the view gives it up when the side turns from the one to the
  other (`TaskPlacement::setSide`), and the panel asks again.
- Join: a pair is a leaf for the gesture, so a cell can be joined INTO a
  neighbour that has a panel (the neighbour's view is closed, its dialog
  with it). Not tested.

**Measured.** `tests/gui/task-panel-side.py`
(`GuiTaskPanelSide_tests_run`): one document, two views one above the
other, the Python console out of the way so that a view has room for a
panel beside it.

| | before (`c969119033`) | after |
|---|---|---|
| as it stood when scored | 23 of 53 | -- |
| as it is | -- | 61 of 61, 8 runs of 8 |

The script was changed after it was scored on the tree before: the views
put one above the other for room, the checks of place split up so that
each prints what it read, and eight added -- what the dialogs are told,
and that Qt has nothing to say about the stack. The 23 that passed there
are checks of what a panel over the view does too. What moved:

| | before | after |
|---|---|---|
| "beside the view" | no such choice | the panel in a cell of its own, 240 wide, the view 614, the two in the slot the view had |
| the other cell of the view area | -- | where it was, to the pixel |
| right, top, bottom, left again | -- | each side, the two filling the slot each time |
| the border dragged 60 pixels | -- | the panel 60 wider, the view holding the size |
| a later panel in a view that chose nothing | over the view | beside it |
| what the dialog is told by all of that | -- | nothing |
| a second panel opened straight beside its view | -- | the first deactivated once, no warning |
| the view split | -- | the new cell outside the two |
| the view maximized | -- | its panel still beside it |
| "over the view" again; the dialog closed | -- | no panel cell, the view's cell as it was |
| the view closed under its panel | -- | the dialog rejected, the cell gone |
| a view in a tab of its own | -- | in a view area, its panel beside it |
| the document saved with a panel beside a view, reopened | -- | two cells as large as before, no panel cell |

By hand, with the first form of the script -- two views side by side in
the small window, 427 pixels each: the pair takes 27 pixels from the
neighbour (454 and 400); with the panel over the view again, and with the
dialog closed, the view's cell is 427 wide again, where it stayed at 454
before the neighbours' sizes were remembered.

The seventeen GUI entries nearest the change pass.

**Not done.** A document saved while a pair is squeezing its neighbour
saves the squeezed proportions. The handle between a cell and its panel
cell has no menu. The unified canvas with a panel cell was not run. A
panel beside a view that is no 3D view was not run. The dock overlays
are not looked at: a panel cell is under an overlaid dock like any cell.

### 15.11 Milestone 3, built (2026-10-07, `7a45fa84b4`)

Overlay mode: 15.5, and most of what 15.7 kept for milestone 4.

**What the user sees.**

- A panel over its view has the look of an overlaid dock: the same title
  strip with the same kind of buttons, the picture showing between and
  around the task boxes, the boxes and the widgets in them as the dock
  overlay's style sheet draws them.
- It is the whole length of its side: left, right, top or bottom, by the
  menu of the title bar, or by dragging the title bar -- across for left
  and right, up or down for top and bottom, to the half of the view the
  pointer is let go in.
- A dotted grip runs along its inner edge. Dragged, it makes the panel
  wider or taller, and the view keeps the size.
- A click on the clear ground between the boxes is a click in the view,
  and the wheel there zooms the view. Over a box the wheel is the
  panel's.
- Sent beside its view or to the combo view, the panel is an ordinary one
  again.

**What is built.**

- `OverlayTabWidget::applyOverlayLook(widget, enable)`, a public static:
  the walk `setOverlayMode` makes over a dock, for a widget that is in no
  overlay tab widget. Unchanged in what it does -- frameless and a
  translucent ground for each widget, a `TaskBox` and a dialog left as
  they are, a combo box's list not entered.
- `TaskPanelHost::applyLook()`: over the picture the host has no ground
  (`WA_TranslucentBackground`, no auto fill), wears
  `OverlayManager::getStyleSheet()`, and runs the walk over its page; in a
  cell of its own all of that is undone. `takePage()` undoes the walk
  whenever a page leaves.
- The header is a subclass of `Gui::OverlayTitleBar`, so the style sheets'
  `Gui--OverlayTitleBar` rule is its ground. Its own handling of the mouse
  -- which drags docks -- is overridden; painting is the styled ground and
  nothing else, the title being a label. The two buttons are made by
  `OverlayTabWidget::createTitleButton` from actions, which names a button
  after its action's data: the names the tests knew are kept.
- `Gui::OverlaySizeGrip` along the inner edge, remade when the side turns
  between down and along. It says where it is dragged to; the host lays
  itself out to that and writes `Task_Size` when the grip is let go.
- **The mask** (`updateMask`): the title bar, the grip, what stands beside
  the scrolling in the page, and each box of the panel cut to the scroll
  area's viewport. Set again whenever the host is laid out, resized, the
  panel's layout changes or it is scrolled. Outside it the host does not
  exist for Qt: nothing is painted and no event is delivered, so the view
  beneath is drawn and takes the pointer by itself.
- `place()`: the room is the cell less its own chrome, as before; across,
  what the grip is being dragged to, else the view's size, else what the
  panel asks for within a third of the view (half, for top and bottom).

**Found on the way.**

- **A host with the keyboard in it, moved or hidden, gave the keyboard to
  the neighbouring view.** Re-parenting a widget hides it for the moment,
  Qt drops the focus it holds and hands it to whatever comes next, and the
  view area made that view the active one: the panel's dialog was
  deactivated for a panel that had only changed its place. It has been so
  since milestone 3 of sec 14 -- going to the combo view with the focus in
  the panel did it -- and no test saw it, their panels recording neither
  call nor being typed into before they moved. `attach()` now puts the
  keyboard in the panel's own view while the host moves and back into the
  panel after; `release()` leaves it in the view.
- **The look was checked against the thing itself.** Three pictures of a
  sketch being edited -- the panel over the view on the left and on the
  top, and beside it -- and a fourth of the same sketch with the panels in
  the combo view and every dock overlaid. In the default theme the
  overlaid Tasks dock has button texts light on light, a dark translucent
  list, box headers as ever and the picture between the boxes; so has the
  panel over its view. The rough edges are the style sheet's, with no
  application theme under it, and the two are the same.
- **The report view came up over the pictures** for "Cannot find icon:
  Std_Point", a warning of the sketcher's own in this tree. Not chased.

**Where it departs, mine.**

- **Pass-through is a mask, not the dock overlay's test of a grabbed
  picture.** The dock overlay asks the alpha of the pixel under the
  pointer and forwards the event; a panel in a view knows where its boxes
  are. So a click INSIDE a box on a spot the style sheet leaves see-through
  is the panel's here, where over a dock it might go through. No
  "mouse pass through" mode, no Escape to leave it: there is nothing to
  leave.
- **No outline effect.** The dock overlay draws an outline round text on
  clear ground (`OverlayGraphicsEffect`), with values the style sheet gives
  an `OverlayTabWidget` by name. A task panel has no text on clear ground
  -- it is all in boxes -- and the effect renders its widget off screen at
  every repaint. Left out.
- **The style sheet is taken when the look is put on.** A change of theme
  while a panel stands over its view reaches it at its next move.
- The menu is one button with a menu, where a dock's title bar has a
  button per choice.

**Measured.** `tests/gui/task-panel-overlay.py`
(`GuiTaskPanelOverlay_tests_run`): one document, two views one above the
other. The wheel is the real pointer's (XTEST).

| | before (`50560fcf27`) | after |
|---|---|---|
| as it stood when scored | 18 of 45 | -- |
| as it is | -- | 47 of 47 |

Changed after it was scored: the grip's step put behind the title bar's
(the size is given up when the side turns), and the check of what the
dialog is told split in three. What moved:

| | before | after |
|---|---|---|
| the host's ground, style sheet, title bar, buttons | opaque, none, a plain widget, plain tool buttons | none, the overlay's, `Gui::OverlayTitleBar`, `Gui::OverlayToolButton` |
| its extent | as tall as its panel: 240 by 197 in a view 409 high | the whole side: 240 by 371 |
| the pixel on the ground clear of its box | alpha 255 | alpha 0 |
| the widget under that point | the panel's scroll bar | the view's |
| the wheel there | nothing | the view zooms |
| the wheel over a box | nothing | nothing |
| top, bottom | taken for the left | the whole width, at the top and at the bottom |
| a grip | none | along the inner edge; 60 pixels dragged, 60 wider, the view holding 300 |
| the title bar dragged down | stays on its side | on the bottom |
| going beside the view with the keyboard in the panel | the dialog deactivated | told nothing |
| the page beside the view, and in the Tasks tab | a plain page | a plain page |

`task-panel-in-view.py` asks for the whole height where it asked for the
panel's own; its other 88 checks are as they were. The four task panel
tests pass together (47, 61, 49, 89), and the nineteen GUI entries nearest
the change.

**Not done.**

- **Standing clear of the dock overlays** (sec 5.3). A panel over a view
  at the edge of the window is under an overlaid dock on that edge, or
  over it. The one part of 15.7's milestone 4 that is left.
- Kept for views that are no 3D views (15.6, M5).
- A view too small for a panel still keeps the title bar alone.
- The new strings are untranslated.

### 15.12 Clear of the dock overlays, built (2026-10-07, `08cc6a8252`)

Sec 5.3, as far as it concerns a panel OVER its view; one beside its view
is a cell like any other and needs none of it.

- `OverlayManager::occupied(widget)` answers the parts of a widget that
  overlaid docks stand over: each overlay tab widget that holds a dock, is
  shown and is not hidden to its hint (`getState() <= Normal`), mapped into
  the widget and cut to it. `OverlayManager::layoutChanged()` is emitted
  after every pass of the docks' layout (the timer that runs `onTimer`),
  whichever way the pass ends.
- A host's room (`roomIn`, `TaskPanelHost.cpp`) is its place less the
  place's own chrome, and now less those strips: each cuts the room on the
  edge of the place it lies along. A strip from edge to edge lies along
  the edge between them; one in a corner cuts the edge it reaches in from
  the least. The host lays itself out again when the docks do.
- The other direction needed nothing. The dock overlay's own handling of
  the pointer asks what tab widget is under it, and a host is none; a
  panel's mask keeps it from covering what it does not draw.

Measured, `tests/gui/task-panel-clear-of-docks.py`
(`GuiTaskPanelClearOfDocks_tests_run`): one view, the tree (260 wide), the
combo view (150) and the Python console (208 high) laid over the window by
`Std_DockOverlayAll`.

| | before (`7a45fa84b4`) | after |
|---|---|---|
| checks | 7 of 16 | 16 of 16 |
| the panel on the left | from x 4, under the tree, down behind the console | from x 260, where the tree ends, to above the console |
| on the right | under the combo view | ends where the combo view begins |
| on the bottom | the whole width, under all three | between the tree and the combo view, above the console |
| the docks docked again | -- | at the left edge of its view |
| what the dialog is told | nothing | nothing |

Not done: a dock that slides out when the pointer nears its hint is
"shown" only once it is out, so a panel does not make room for it
beforehand; the reveal strip of a hidden dock (`DockOverlayHintSize`) is
not kept free.

### 15.13 Kept for a view that is no 3D view, built (2026-10-07, `0a95038b3d`)

Sec 15.6: "4, for all types of view".

**What the user sees.** A panel sent into a drawing page's view, put
beside it or over it on a side of his choosing, is there again the next
time the document is opened and a panel is opened in that page.

**What is built.**

- `Gui::Document::ViewTaskState` (place, mode, side as the texts the
  properties hold, and the size) and `savedViewTaskState(view)`, by the
  token the saved layouts name an object's view by, `O:<object>`
  (`objectViewToken`).
- `GuiDocument.xml`: `viewstates="K"` on the `Camera` element, and K
  entries after the view areas:

      <ViewTaskState view="O:Page" place="InView" mode="Side" side="Right" size="0"/>

  The reader takes its counts from attributes of `Camera` and then reads
  to the end of the document, so a reader from before this passes the
  entries over, and a file from before it has none. Read from the code;
  no older build was on hand to open such a file with.
- `TaskPlacement` asks the document for a view that is no 3D view and has
  no property of its own. NOTHING is written into the view for it: a
  document reopened, with a panel opened in a page, is not modified by
  that. A property is made when the user chooses, as for every view; and
  one emptied -- the view following the preference again -- is made even
  so, to stand in front of what was saved.
- On save: what each open view holds (`TaskPlacement::ownState`), and what
  was read for the views nobody opened this session, for as long as their
  objects exist.

**A view is known for its object's by its name.** The saved layouts
already depend on it: an object's view carries the object's name as its
widget's object name (`TechDrawGui::MDIViewPage::setDocumentObject`), and
the object's view provider answers `getMDIView()` with it. **A
spreadsheet's view carries no such name.** It is in no saved layout for
that reason, and its panel's place is not kept for the same one. Giving it
the name is a line; it would also put spreadsheet views into the saved
layouts, so that a document reopens with them, and that is a change to
what opening a document does, not made here. Within a session a
spreadsheet view holds its place like any view: the test first ran on
one, a panel over it and beside it.

**Measured.** `tests/gui/task-panel-kept-any-view.py`
(`GuiTaskPanelKeptAnyView_tests_run`): a drawing page, its panel sent into
its view and put beside it on the right; saved, reopened; put over the
view; saved, reopened.

| | before (`7a45fa84b4`) | after |
|---|---|---|
| checks | 9 of 15, with a spreadsheet's view in the page's place | 15 of 15 |
| the saved file | no entry | one, for `O:Sheet`, and `viewstates="1"` |
| the view after the reopen | a new one, holding nothing | a new one, holding nothing |
| its next panel, the preference saying "combo view" | in the Tasks tab | beside it on the right |
| changed to "over the view", saved, reopened | in the Tasks tab | over the view |

Scored on the tree before with a spreadsheet's view, which is what the
script first used; with the page it was not scored there. The six that
failed are the six that ask for anything to be kept.

### 15.14 Where sec 15 stands

Built, each with its test and its suites: 15.9 to 15.13. Rows in
`docs/Testing.md`.

Left, in the order I would take them:

1. **A spreadsheet's view** (15.13): one line, and a decision about
   reopening documents with it.
2. **The style sheet followed live** (15.11): a theme changed while a
   panel stands over its view.
3. **A dock that slides out**, and the hint strip (15.12).
4. **The join gesture into a cell with a panel**, the handle's menu, a
   document saved while a pair squeezes its neighbour (15.10): none tested.
5. **The unified canvas** with a panel over a view and with a panel cell:
   not run in any of this.
6. Strings untranslated. `pre-commit` and `clang-format` are not on the box
   this was written on; the format hook ran on none of it.
7. M5 of sec 9, the browser: a served client's panel is where it was.
