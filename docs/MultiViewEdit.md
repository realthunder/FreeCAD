# Multi-view editing: view state per view, edits per view

Status (2026-09-25): **idea recorded, not built.** Written down at the
user's request so it can grow before it is implemented. Nothing here has
code yet; where it names code, that is the code as it stands, which the
idea starts from.

Companion documents:

- `docs/TransactionLog.md` sec 24.9-24.10 -- what the document log records
  of view providers today, and why the camera was kept out of it.
- `docs/ThinClient.md` 8.11 (the shared session) and 8.12 (what per client
  would cost) -- the multi-client side this idea answers much of.
- `docs/ViewSettings.md` -- per-view settings that override preferences,
  and what travels in a document.
- `docs/CoinRetirement.md` 5.9 and 5.12 -- the two per-view maps that
  already live on the view (`ObjectDisplayModes`, `OnTopObjects`).
- `docs/SplitViews.md` -- views with persistent names, which is what makes
  per-view state addressable across a save.

## 1. The idea (user, 2026-09-25)

A document's state splits in two, and the split is by *what the state
is about*, not by who changed it:

- **Document state** -- how the model is. Geometry, parameters, and the
  model's appearance: colours, materials, line widths. Shared by every
  view and every client, recorded by the transaction log, undone by the
  document's undo.
- **View state** -- how one view is looking at the model. The camera, the
  view's draw style and render settings, which objects it shows, which it
  draws on top, which display mode it forces. Owned by one view; every
  view has its own.

Two mechanisms follow:

1. **View state lives on the view, as view properties.** Each 3D view
   (`View3DInventor`, already a property container) carries its own values
   -- the camera included, as a plain string property.
2. **Each view has its own change stack, session only.** Changes to a
   view's properties are undone and redone per view, independently of the
   document's undo, and the stack is not saved and not logged. It dies
   with the view.

And a third, larger step that the first two make possible:

3. **The edit session moves from the document to the view.** Upstream
   FreeCAD has one edit per document (one object in edit, one task
   dialog); this fork extends it to one per view, so two views -- two
   windows, or two clients -- can edit different things at once.

Why it matters beyond tidiness: **each client of a served document has
its own view** (a mirror, `docs/ThinClient.md` 8.3). If view state is per
view, per-client camera, visibility and display modes come for free, and
several of the per-client items of `docs/ThinClient.md` 8.12 stop being
problems. The whole question then becomes *which properties are view
state* (section 3).

## 2. Where things stand

**Already per view** (on `View3DInventor`, saved in the view's `<View3D>`
entry of `GuiDocument.xml`): `DrawStyle`, `ShadingType`,
`ExternalRenderType`, `ShowNaviCube`, `ThumbnailView`, the light and render
overrides of `docs/ViewSettings.md`, and two per-object maps --
`ObjectDisplayModes` (a display mode forced on an object in this view,
`docs/CoinRetirement.md` 5.9) and `OnTopObjects` (5.12). The second of
those moved *off* the App document onto the view on 2026-08-27, which is
this idea applied once already.

**The presentation pattern exists.** `ViewProviderDocumentObject::
DisplayModeInView` is a transient property row on the view provider that
reads and writes the *active view's* `ObjectDisplayModes` entry for its
object. The storage is the view's; the property editor edits it where the
user expects to find it. Per-view visibility would be the same shape.

**The camera is ad hoc XML.** `Gui::Document::SaveDocFile` writes a
`<Camera>` element (the first view's settings, with `extra`, `id`,
`binding`, `view3d`, `viewareas` attributes), a `<CameraExtra>` per further
view, then a `<View3D>` per view (its properties) and a `<ViewArea>` per
split layout. `RestoreDocFile` reads them back into `_savedViews`,
`_view3DContents`, `_viewAreaLayouts` and rebuilds the views. The camera
is not a property and is in no undo.

**Visibility is two properties, both document state.**
`App::DocumentObject::Visibility` ("visibility in App name space") and
`ViewProviderDocumentObject::Visibility`, kept in step. Since
`docs/TransactionLog.md` 24.10 both are logged and undone as document
data. `TempoVis` (the edit mode's temporary hides, a saved Python property
on e.g. `ViewProviderSketch`) writes the same visibility.

**The edit session is per document.** `Gui::Document::_editViewProvider`
(one object in edit), `_editingViewer`, the one editing root, and
`Gui::Control()` with its one active task dialog. The shared session of
`docs/ThinClient.md` 8.11 made that the definition: every view of the
document *joins* the one session (`View3DInventorViewer::joinEditing`),
so a sketch entered in one window is drawn in from any of them.

## 3. Which properties are view state

The test: **if a second person opens the same document next to you, should
your change change what they see?** Yes is document state; no is view
state. Applied:

| Property | Where now | Class | Note |
|---|---|---|---|
| camera (position, orientation, height/focal) | `<Camera>` XML | view | becomes a string property of the view |
| `DrawStyle`, `ShadingType`, render and light overrides | view | view | as today |
| `ObjectDisplayModes`, `OnTopObjects` | view | view | as today |
| object visibility (view provider `Visibility`) | view provider | **view** | the user's example; per view, seeded as below |
| `App::DocumentObject::Visibility` | App object | document | the seed for new views (section 4.3) |
| `ShapeAppearance`, `ShapeColor`, `LineColor`, `PointColor`, colour arrays, `Transparency`, `LineWidth`, `PointSize` | view provider | **document** | the user's example: colours are the model's look |
| `DisplayMode` (the object's own) | view provider | document | the per-view override is `ObjectDisplayModes` |
| `TempoVis` | view provider | view | the hides of an edit, which becomes per view (section 6) |
| `Selectable`, `ShowInTree`, `OnTopWhenSelected`, `SelectionStyle` | view provider | open | interaction preferences; see section 8 |
| `BoundingBox`, `Deviation`, `AngularDeflection`, `Lighting` | view provider | open | display quality; plausibly document (how the model is shown by default) with a per-view override later |

The camera and the view's settings are uncontroversial. Visibility is the
one that matters most and the one that changes behaviour, which is why
section 4 is mostly about it.

## 4. The view property mechanism

### 4.1 Storage on the view

View state is a property of the view. A per-object value is an entry of a
map on the view keyed the way `ObjectDisplayModes` and `OnTopObjects`
already are: an internal name, or `Doc#Name` for an object of another
document shown through a link, with a subname path where the value is per
occurrence. Per-object visibility becomes such a map (a hidden-object set,
or an explicit visible/hidden override per object -- to be decided, 8).

### 4.2 Presentation on the view provider

The property editor shows view state where users look for it -- on the
object -- through the `DisplayModeInView` pattern: a transient row on the
view provider that reads and writes the active view's entry. The view
provider's own `Visibility` row becomes that presentation. The many call
sites that read `ViewProvider::isShow()` or `Visibility.getValue()` need a
view to ask: the active one by default, the rendering view where there is
one (`ViewerContext::current()` is the existing seam).

### 4.3 Seeding and the primary view

A view needs values before anyone set any. **The App-space visibility is
the seed**: a new view shows what `App::DocumentObject::Visibility` says,
and copies nothing else from other views, except where a view is split
off another (`docs/SplitViews.md`), where it starts as a copy of its
parent.

**Only the host's primary view writes back.** Hiding an object in the
desktop's primary 3D view also sets the App visibility -- the document
still remembers what its author showed, the file still opens looking the
same, and scripts reading `obj.Visibility` keep working. Hiding it in a
second window, or in a client's view, changes that view only. Which view
is primary is a property of the Gui document (the first desktop view by
default); a served document's clients are never primary.

### 4.4 Saving

A desktop view's properties are saved in its `<View3D>` entry, as today,
and restored onto the view with the same persistent name. A client's
mirror view is not saved: its state is session state. The camera moves
into the view as a string property (`Camera`), saved with the rest of the
view. For readers of older formats the old `<Camera>`/`<CameraExtra>` XML
is still written when the document is saved at schema 4 and not at
schema 5 (user ruling, 2026-09-24); the reader takes both.

## 5. The per-view change stack

Each view keeps its own undo and redo stack of view-property changes:

- **Session only.** Not saved, not in the transaction log, gone when the
  view closes. The document's undo never touches view state, and a view's
  undo never touches the document.
- **Coalesced.** A camera orbit is one entry when the interaction settles,
  not one per frame; a run of changes to one property in one gesture is
  one entry.
- **Its own commands.** An "undo view" / "redo view" pair, the precedent
  being SolidWorks' Ctrl+Shift+Z "Previous View" and Rhino's `UndoView`
  (`docs/TransactionLog.md` 24.10 has the survey). Which view it acts on:
  the active one.
- **Per client for free.** A client's view has its own stack; one client
  undoing a camera move or a hide does nothing to anyone else.

This is the mechanism the user ruled the camera should get instead of
the document log ("bundled with other property ops it will cause
trouble"), and the other view settings with it.

## 6. The edit session per view

Upstream FreeCAD edits one object per document at a time; this fork's
shared session (`docs/ThinClient.md` 8.11) keeps that and lets every view
join it. The extension is to allow **one edit session per view**: a view
may join the document's session, as now, or own one of its own, editing
another object while a different view (a window, a client) edits
something else.

What that touches, from `docs/ThinClient.md` 8.12:

- **D. The edit session.** `Gui::Document::_editViewProvider`,
  `getInEdit()`, `_editingTransform`, `signalInEdit`/`signalResetEdit`,
  `ViewProvider::setEditViewer`, the editing root: a view-to-session map,
  `getInEdit()` answering for the current view, an editing root per
  session. `ViewerContext::editViewProvider` is the seam that exists.
- **An object lock.** Two sessions must not edit one object: entering
  edit takes a per-object lock the other views see.
- **E. Chrome.** `Gui::Control()` has one active dialog; per-view sessions
  need a task panel per session (per view, or per client).
- **TempoVis per view.** An edit's temporary hides are view state by
  section 3: the view in edit hides, the others do not. That removes a
  whole class of "why did my other window change" surprises and the
  hides-as-document-state compromise 8.11 accepted.
- **Transactions.** Each session's edit is still a document transaction
  (the log records it); two sessions' transactions interleave in one log,
  which is what the per-session undo of `docs/ThinClient.md` 8.12 G and
  the selective undo of `docs/TransactionLog.md` 24.4 are for.

## 7. What it changes elsewhere, when built

- `docs/TransactionLog.md` 24.10 logs the view provider's `Visibility` as
  document data. Once visibility is view state, that row leaves the log;
  the App visibility stays in it (it is the document's record of what the
  author showed). `TempoVis` leaves it too.
- `docs/ThinClient.md` 8.12: A (the view) grows to hold the view state; E's
  "per-view visibility overrides, which exist today only as the browser's
  local hide" becomes the general mechanism; D becomes section 6.
- `docs/ViewSettings.md`: the per-view settings join the per-view change
  stack.

## 8. Open questions

- Visibility storage: a set of hidden objects, or a full override map
  (visible, hidden, follow-the-seed)? The second lets a view say "follow
  the document" per object.
- Where `Selectable`, `ShowInTree`, `OnTopWhenSelected`, `SelectionStyle`
  belong. They are about interaction rather than the model's look, which
  argues for view state; but a document author sometimes sets them on
  purpose for every reader (an unselectable reference body).
- Display quality (`Deviation`, `AngularDeflection`, `Lighting`,
  `BoundingBox`): document defaults with a per-view override, like display
  mode, or view state outright?
- Which view is primary when the desktop has several windows, and whether
  the user can change it.
- Whether the per-view change stack also takes view-affecting commands
  that are not properties (a section plane, a clipping box).
- Link and assembly occurrences: visibility per occurrence (subname) is
  already how `OnTopObjects` keys; `LinkVisibility` and element visibility
  (`setElementVisible`) need a place in the same scheme.

## 9. Order of work (later)

1. The camera as a view property, the old XML kept at schema 4 only.
2. The per-view change stack, with the camera and the existing view
   properties in it, and the undo-view/redo-view commands.
3. Visibility as view state: the map on the view, the presentation row,
   the seed and the primary-view write-back; the log's `Visibility` row
   changes with it.
4. The remaining classifications of section 8.
5. The edit session per view (section 6), on top of the shared session.
