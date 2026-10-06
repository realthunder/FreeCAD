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
design, is sec 11: milestone 1 so far.

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
