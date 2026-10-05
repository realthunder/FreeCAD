# Omni search -- `/` over objects, commands and parameters

Status: implemented on the desktop (2026-09-11) and mirrored in the browser
viewer (2026-09-12, section 6).

FreeCAD had three places to find something by typing, none of them reachable
from one key: the tree's search box (objects only, `Gui::ExpressionLineEdit`
in `Tree.cpp`), the command completer inside Tools > Command history
(`Gui::CommandCompleter`), and nothing at all for the several hundred
generated application parameters (`ViewParams`, `TreeParams`,
`RenderParams`, ... -- editable only through the preference pages).
`Std_OmniSearch` (Tools menu, default shortcut `/`) puts the three behind one
floating box.

## 1. Using it

Press `/` with the 3D view or the tree focused. A box opens at the top centre
of the active view with `/` already typed and three suggestions:

| prefix    | mode      | what the rest of the line means                         |
|-----------|-----------|---------------------------------------------------------|
| `/ `      | objects   | a document, object, sub-object or property path         |
| `/cmd `   | commands  | keywords over every registered command                  |
| `/param ` | params    | keywords over every generated application parameter     |

Pick one (Enter, Tab or click) or type the prefix; the space ends it. Text
that does not start with `/` is an object query as typed.

In every popup, Up/Down and Shift+Tab move the highlight, and Tab or a click
picks the highlighted row (the first one when none is); Enter picks it too
except in the object popup, where Enter acts on the text as typed (below).

**Objects.** The completer is the expression completer, so it offers
documents, objects by name or `<<label>>`, sub-objects and properties as you
type, with the same "Exact match / Case sensitive / Unfiltered" options in
the context menu. While the text names an object the tree scrolls to it and
preselects it, exactly as the tree's own search box does. Enter selects it
and pops the hierarchy menu of `Std_SelUp` -- the document and the parents
down to the object -- so the next step up or down is one click. When the
text names a property (`Box.Length`, `Part.Box.Placement`) and the row is
picked (Tab or click) or Enter is pressed, an editor for it appears under
the line: the same editor the property view would show, built from the same
`PropertyItem`, with an `f(x)` toggle to edit the expression instead when
the property is bound. Moving the highlight through the popup only splices
the completion into the text and tracks it in the tree; no editor is built
until the row is picked. A change applies through the property item as a
Python command; the edit is one undo step, and the document recomputes when
the box closes. Enter in the editor closes the box.

A leading dot is the selection: with objects selected when the box opens,
`.` lists the properties every selected object has with the same name and
type -- the expression completer's "member of this object" shorthand
(`ExpressionCompleter::setLocalObjects()`), widened from one owner object
to several -- and `.Height` edits that property on all of them at once, the
way the property view edits a multi-selection (one `PropertyItem` over
every property, one transaction, the panel titled "N objects . Height").
The first selected object is also what full paths are parsed against.
With nothing selected the dot lists nothing and resolves nothing. Where a
label is the object's name the completer no longer offers the `<<name>>`
row beside the name; a document behaves the same.

A `#` addresses a document, as in the expression grammar's `Doc#Box`,
with two additions the grammar does not have. `#` alone is the document
being searched, so `#Box.Length` is that document's `Box` (the same as
`Box.Length`, but it survives a label that clashes with an object name in
another document). And `#` followed by a dot names a member of the document
itself: `#.Comment`, `Doc#.Comment`, `<<Label>>#.Comment` resolve to the
`App::Document` property and open its editor exactly as an object property
would, with `Doc#` as the title; `#.View2.DrawStyle` is a property of the
document's view `View2` -- every view carries a persistent name, `View1`,
`View2`, ... (`BaseView::getPersistentName()`, the `Name` attribute in
Python), and the `View3DInventor` is a property container of its own, with
`DrawStyle`, `ShadingType`, `ShowNaviCube` and so on -- and
`#.ActiveView.DrawStyle` the same on whichever view is active. After `#.`
or `#.View2.` the popup lists that container's properties -- name and
documentation, hidden ones left out -- and under the document one row per
view, `ActiveView.` first, with the view's window title as the description;
picking one opens that view's list. The `ViewArea` containers that host
views are not listed. This popup is
a keyword filter over `OmniSearch::documentMembers()`, not the expression
completer, whose model has no row for a document's own properties, and the
expression engine itself is unchanged: `#.Comment` is the box's grammar,
not an expression.

The view side of an object is its `ViewObject`, the Python name:
`Box.ViewObject.ShapeColor`, `Part.Box.ViewObject.Visibility`, and
`.ViewObject.Visibility` for every selected object at once. `ViewObject`
is a new pseudo property of `App::ObjectIdentifier` (beside `_self`, `_shape`
and the others), so the expression completer offers it under every object
and, below it, the view provider's Python attributes -- the properties among
them carry the property icon; the box resolves `ViewObject` followed by one
name to the view provider's property through `Gui::Application`. In an
expression `Box.ViewObject.Visibility` evaluates through Python, as `_self`
does, and is `None` without a GUI.

**Commands.** Rows show the command's icon, title and shortcut and its
tooltip as the description; inactive commands are greyed and inert. A group
command (`Std_DrawStyle`, a workbench's tool groups) carries an arrow: click
it, or press Right on the row, for the group's menu -- the same radio-button
or check-box rows as the toolbar button's drop-down, because it is built by
the same code (`ActionGroup::populateMenu`). Enter on a row runs the command
and records it in the command history (`Std_CmdHistory`). Matching is by
keyword: every whitespace-separated word must occur, case-insensitively, in
the title, internal name, shortcut or description.

**Parameters.** Rows are the parameter's path with the `User parameter:`
prefix and the `BaseApp` root every generated parameter shares dropped,
leading slash included -- `Preferences/View/SyncSelect`
(`App::ParamInfo::displayPath()`; a path not under `BaseApp` keeps its
leading `/`) -- with
the parameter's title and documentation as the description and its current
value on the right; the tooltip carries the full path. Keywords match the
full path, the accessor's `Namespace::Class::Name`, the title and the
documentation. Choosing a row shows the parameter's editor under the line,
labelled with that path, its title and its default. The editor is the
one its preference page uses -- a combo box with the same items, a colour
button, a file chooser, a shortcut editor, a spin box with the same range --
or, for a parameter whose page widget the registry cannot describe, a basic
one for its value type. Edits apply immediately (the generated observer
classes refresh their caches, so the effect is visible at once); Reset
removes the stored value so the default applies again.

Esc and a click elsewhere work in two steps: with a popup up they close the
popup and leave the box; without one they close the box, panel or no panel.
The box undoes what it changed transiently -- the tree highlight, the
preselection -- when it closes.

## 2. The pieces

```
App::ParamRegistry            src/App/ParamRegistry.*        every generated parameter, get/set/reset
   ^  registrars              generated into every XxxParams.cpp by Tools/params_utils.py
Gui::OmniSearch               src/Gui/OmniSearch.*           grammar, resolve, search, editor factory
Gui::ParamListModel, Gui::KeywordFilterModel                 (same file)
Gui::CommandListModel         src/Gui/CommandCompleter.*     the command list, lifted out of the completer
Gui::OmniSearchBox, Gui::OmniSearchEdit   src/Gui/OmniSearchBox.*   the floating box
Std_OmniSearch                src/Gui/CommandStd.cpp, Workbench.cpp (Tools menu)
Gui::OmniControl              src/Gui/OmniControl.*         the ops of the browser mirror (section 6)
                              src/Gui/SceneControlP.h       the descriptor shared with SceneControl.cpp
OmniBox                       src/Gui/Renderer/web/src/omni.tsx        the box in the viewer
Catalog                       src/Gui/Renderer/web/src/omnicatalog.ts  the versioned lists
```

### 2.1 `Gui::OmniSearch` -- the search without the box

`OmniSearch.h` is deliberately free of widgets (the one exception builds one
and returns it). It is what the box consumes and what the browser tier will
consume:

- `parseInput(text)` -> `{mode, query, offset}`; `modePrefix(mode)`.
- `resolveObject(query, owner, match)`: `App::ObjectIdentifier::parse()` of
  the query relative to `owner` (any object of the document to search). A
  real, non-pseudo property resolves to a property match (a pseudo property
  parses to a stand-in, the object's `Label`, so the check is the `ptype`
  out-parameter of `getProperty()`, not `isPseudoProperty()`); the pseudo
  property `ViewObject` followed by one name resolves to the view provider's
  property; otherwise the tree's trick -- append `._self`, a pseudo property
  every object answers to, and accept if the parse lands on it -- resolves
  an object path. Before any of that, a `#` outside a `<<label>>` is the
  document separator: `#.X` and `Doc#.X` resolve on the document itself
  (`ObjectMatch::doc` set, `obj` empty), `#.View2.X` on the view of that
  persistent name and `#.ActiveView.X` on the active one (the active MDI
  view may be a `ViewArea`; `activeSubView()` is the view inside), and a
  leading `#` before an object name is dropped. The owner
  comes from `TreeWidget::startItemSearch()`, which also sets up the tree's
  search state.
- `resolveInDocument(query, doc, match)`: the same over a document with no
  selection to take the owner from -- the owner is the document's first
  object, and the `#.` forms work on an empty document. The match names
  the view (`ObjectMatch::view`, `ActiveView` resolved to its name) so a
  consumer can address it again. `documentView(doc, name)` is the
  container behind `ActiveView` or a persistent name, `documentViews(doc)`
  the names a `#.` can offer.
- `documentMembers(head, owner)` and `splitMemberQuery(query, head, tail)`:
  the rows behind a `#.` or `#.View2.` popup, and the split of a query
  into the head that names the container and the member typed so far.
- `searchCommands(query)`, `searchParams(query)`: plain-data results.
- `createParamEditor(info, parent)`: the `Gui::PrefWidget` for a parameter,
  bound (`setEntryName`, `setParamGrpPath`) and restored.
- `ParamListModel` over `ParamRegistry::entries()`; `KeywordFilterModel`, a
  proxy keeping the rows whose search-text role carries every keyword.
- The role numbers (`OmniSearch::Roles`) line up with
  `CommandListModel::Roles`, so one delegate and one filter serve every list.

### 2.2 The box

`OmniSearchEdit` is one `QLineEdit` with five completers -- the chooser,
`Gui::ExpressionCompleter` for objects, keyword-filtered `QCompleter`s
over the command and parameter lists, and one more keyword-filtered list
for the members after `#.` (rebuilt from `documentMembers()` on every
edit). `parseInput()` on every edit decides which one answers; at most one
popup is up. The expression completer only
ever sees the query, so its completions are spliced back with the prefix's
offset (`completeObject()`, the eight lines of
`ExpressionLineEdit::slotCompleteText()`, plus one fix-up: for a member of
the owner object the model completes to the expression shorthand `.Length`,
and since the owner is only the document's first object the splice keeps
the typed object in front of the dot); it never sees the `#` of `#Box`
either (`objectSkip()`), since the grammar only knows `Doc#Box`. Its
`highlighted` signal only
splices, its `activated` signal (a click) splices and then commits
(`activateObject()` -> `objectActivated`), which is what builds the
property editor. The member completer works the same way
(`completeMember()`), except that picking a view row (`View1.`) re-runs
the query to open the next level instead of committing.

Keys are handled on the popups, not on the edit: while a popup is up the
key events go to it, and `QCompleter` forwards them to the edit's `event()`
directly, past any filter installed on the edit. So `OmniSearchEdit` is an
event filter on all four popups, and it re-installs itself on the object
popup after every `slotUpdate()` because `ExpressionCompleter`'s lazy
`init()` re-sets the popup and would otherwise move its own filter ahead
(it turns Tab into Down and swallows it). The filter makes Tab pick the
current row (`chooseCurrentRow()`, the first row when none is current) and
Shift+Tab move up; on the object popup it also takes Return, because
`ExpressionCompleter` hides the popup and lets the key fall into the list
view where the box would never hear it.

`OmniSearchBox` is a frameless `Qt::Tool` window over the main window. It
holds the edit and two panels: `OmniPropertyPanel` builds the property
editor the way `PropertyModel`/`PropertyItemDelegate` do
(`PropertyItemFactory::createPropertyItem(prop->getEditorName())`,
`setPropertyData`, `createEditor`/`createPropertyEditorWidget`/
`createExpressionEditor`) and wraps the edit in a transaction the way
`PropertyEditor::openEditor()`/`closeTransaction()` do; `OmniParamPanel`
hosts `createParamEditor()`'s widget with `initAutoSave()` so every change
is stored. Object selection on Enter is `TreeWidget::itemSearch(query,
true)` followed by the `SelectionMenu::onSelUpMenu()` recipe:
`SelUpMenu` + `TreeWidget::populateSelUpMenu(&menu, &objT)` +
`execSelUpMenu`. Group expansion is `cmd->initAction()`,
`qobject_cast<ActionGroup*>(cmd->getAction())->populateMenu(&menu)`.

## 3. The parameter registry

`App::ParamInfo` describes one generated parameter: namespace and class of
the accessor (`Gui`, `ViewParams`), the group path (with any `subpath`
already folded in), accessor name and stored entry name, the value type
(`Bool`, `Int`, `UInt`, `Hex`, `Float`, `String`), the default as text, the
title and documentation (untranslated; the class name is the translation
context, as in the generated pages), and the proxy: the preference-page
widget the parameter was generated with, plus what that widget was set up
with -- combo items (and whether the stored value is the item data rather
than its index), spin-box range and step and decimals, whether a colour
allows transparency.

`App::ParamRegistry` holds them all: `entries()`, `find(path, entry)`,
`search(keywords)`, `getValue`/`setValue`/`reset`/`isSet` through the
`ParameterGrp`, so the generated observer classes (which cache every value)
see the change. The registry is populated at library load: `params_utils.py`
`define()` now emits, after the observer class,

```cpp
static const App::ParamRegistry::Registrar _ViewParamsRegistrar({
    App::ParamInfo("Gui", "ViewParams", "User parameter:BaseApp/Preferences/View",
                   "TransparencyOnTop", "TransparencyOnTop", App::ParamInfo::Float, 0.5)
        .setTitle("...").setDoc("...").setProxy("SpinBox").setRange(0, 1, 0.1, 2),
    ...
});
```

The default is passed as the same C++ expression the getter is seeded with
(it may be a macro, `FC_EXPR_PARAM_EDIT_BG_ALPHA`), and `ParamInfo::Default`
formats it by type. Each proxy class contributes its fields through
`registry_fields()`; a proxy the registry cannot describe (`ViewParams.py`'s
`ParamAnimationCurve`, whose items come from C++ at runtime) keeps its own
class name and `createParamEditor()` falls back to the type's editor. A
custom proxy that *can* be described says so: `OpenViewParams.py`'s
`ParamTargetCombo` registers as a `ComboBox` whose stored value is the item
data.

Regeneration, after editing a `*Params.py` or the generator (cog is not
wired into CMake; `pip install cogapp` once):

    cd src
    python -m cogapp -r -U Gui/ViewParams.h Gui/ViewParams.cpp

One invocation per directory: cog imports the `*Params.py` next to the file
and caches modules by name, so `Mod/Part/App` and `Mod/Part/Gui` in the
same run can hand one the other's parameters (the Gui header came out
without its `on_change` hooks that way).

`-U` matters on Windows: without it cog writes CRLF, and it also skips a
file whose content only differs by line endings, so a CRLF file stays CRLF.
The registrar copies each parameter's doc string a second time, so a `.py`
with non-ASCII in a doc (`RenderParams.py` has a few section signs) yields
two lines the post-commit hook transliterates on every regeneration; the
committed file is the hook's version, and re-cogging shows those lines as a
diff until the next commit strips them again.
The line endings in this repository are frozen (`.gitattributes`), so check
`git ls-files --eol` and match the index -- most generated files are LF,
`Mod/Part/App/PartFeature.cpp` is CRLF.

## 4. Adding to it

- A new parameter or proxy field: `params_utils.py` (`Param.registry_entry`,
  `ParamProxy.registry_fields`), then regenerate; `createParamEditor()` in
  `OmniSearch.cpp` maps the proxy name to a widget.
- A new mode: `OmniSearch::Mode` + `modePrefix` + `parseInput`, a model with
  the shared roles, a completer in `OmniSearchEdit`, and the box's reaction
  to its activation.

## 5. Tests

- `tests/src/App/ParamRegistry.cpp` (in `Tests_run`): the registrars ran,
  defaults format by type, keyword matching, search over path/name/doc, and
  get/set/reset round trips through the parameter group.
- `tests/src/Gui/OmniSearch.cpp` (`OmniSearch_Tests_run`, a Qt test): the
  grammar, `resolveObject` over a document (name, label, property, pseudo
  property, misses, the `#` forms over one and two documents),
  `documentMembers` and `splitMemberQuery`, `searchParams`, the model and
  filter, and `createParamEditor` for every proxy kind and every value type.
  There is no `Gui::Application` in it, so `ViewObject` and `ActiveView`
  are only checked to resolve to nothing there.
- `tests/src/Gui/OmniControl.cpp` (`OmniControl_Tests_run`, a Qt test):
  the ops of section 6 through `handleSceneControlRequest()` -- the
  parameter catalog whole and by delta (a registered parameter is one
  added row for a viewer on the old version, another session's version
  is answered whole), `omni.rows`, `param.get/set/reset` and their
  view-only refusal, `omni.objects` with sub-objects, and `omni.resolve`
  for objects, labels, sub-object paths, properties, `#Name`, `#.`
  members and an empty document. Without a `Gui::Application` the
  command ops answer `NoGui` and views are absent, which is asserted.
- Commands need a `Gui::Application`; they are exercised by hand: `/cmd
  draw` -> `Std_DrawStyle` with its arrow, `/cmd history` -> Enter runs
  `Std_CmdHistory` and it appears in the history.

## 6. The browser mirror

Implemented 2026-09-12. The viewer's DOM layer has the box too
(`src/Gui/Renderer/web/src/omni.tsx`): `/` with the canvas focused, or
"Search" in the viewer menu for a device without a keyboard. The grammar
is the desktop's (`parseInput` is ported line for line), the three modes
are the same, Tab and a click pick a row, Enter acts on the text as
typed in object mode and on the row elsewhere, Esc closes the panel
first and the box second. The desktop's widget-free layer answers it
over the control channel: `SceneControl.cpp` dispatches, `OmniControl.cpp`
implements the ops against `Gui::OmniSearch`, `App::ParamRegistry` and the
command manager, and `SceneControlP.h` shares the property descriptor
between the two. That layer was kept free of widgets for exactly this.

### 6.1 Nothing per keystroke

The box re-filters on every keystroke, and a round trip per key over the
WebSocket is what the design refuses. What crosses the wire, and when:

- **The command and parameter lists are catalogs**, shipped whole once
  and kept by version -- the scene stream's model (SceneStreaming.md
  section 5) applied to two small lists. The viewer states the version it
  holds and the session it came from; the backend answers with the rows
  to add or replace and the keys to drop since then, merged last-wins
  from a bounded history (64 versions), or with the whole list when the
  session is another run's or the version has fallen out of the history.
  The viewer keeps the rows in memory and in `localStorage`
  (`fc.omni.commands`, `fc.omni.params`), so a reload costs a delta, and
  a delta of nothing is about a hundred bytes. The backend's side is
  `OmniControl.cpp`'s `Catalog`: a version bumps when the rows differ
  from the last build, and the rows are rebuilt only when what they
  come from moved -- the command manager's revision and the shortcut
  manager's `shortcutChanged` for commands, the registry's entry count
  for parameters. Every viewer is told `{"op":"omni.changed","list",
  "session","version"}` when a version advances; the cues are a
  workbench activation (commands registered, libraries with parameters
  loaded) and a shortcut change, checked one event-loop turn later, and
  any `omni.catalog` request also refreshes. The box syncs both
  catalogs once when it opens, and filters locally from then on.
  Measured over the live socket against a serving desktop (2026-09-12,
  a dependency-free RFC 6455 client speaking the text lane): the
  command catalog is 596 rows and 74 KB whole at start-up (679 rows and
  86 KB with Draft loaded), the parameter catalog 531 rows and 282 KB
  (the documentation strings are most of it); either answers in 6 to
  10 ms on the loopback, and a "current" answer is 106 to 108 bytes in
  under half a millisecond. Loading the Spreadsheet workbench through
  the channel itself (`command.run` of a `Std_Workbench` row) produced
  both pushes 220 ms later, and the deltas were exactly the new rows:
  16 commands in 1.7 KB, 10 parameters in 3.7 KB. Both catalogs are one
  fetch per browser, ever, until a version moves.
  **Compressed on the wire since 2026-09-12**: the scene socket
  negotiates permessage-deflate and uses it on the text lane, so those
  74 KB and 282 KB leave as 17 KB and 61 KB -- 356 KB of first-connect
  traffic down to 78 KB. Nothing about the catalog changed; the
  transport did. docs/SceneServerPort.md sec 7.8 has the settings, the
  table and why the binary lane is left alone.
- **A parameter's value and a command's active state are not in the
  catalog**: they change without it. They ride `omni.rows` for the rows
  on screen (at most 60), asked once the list has stood still for 120 ms
  and again after every settle; a reply a later keystroke made stale is
  dropped by a sequence number. The desktop shows the same value on the
  row's right and greys inactive commands. Measured: 60 keys answer in
  202 bytes and 0.6 ms.
- **The document's objects** come once per opening (`omni.objects`:
  name, label when it differs, type, sub-object names, and the views a
  `#.` can name). Object rows, `Part.` sub-object rows and the `#.`
  view rows filter locally.
- **Property descriptors** come from the existing `getProperties`, one
  fetch per container (an object with both scopes, the document, a
  view) cached for the box's life; the rows after `Box.`,
  `Box.ViewObject.`, `.`, `#.` and `#.View2.` filter locally and carry
  the descriptor, so picking one opens the editor with no further round
  trip.
- **`omni.resolve`** runs the desktop grammar on the backend for the text
  as typed -- Enter, or a click on an object row -- and answers an
  object, or a property with its descriptor and the addressing
  `setProperty` needs. It is the one op that sees the typed text, and
  only when the user has stopped typing. Measured: 0.2 to 2.5 ms and
  under 400 bytes, except the very first resolve of a process (411 ms,
  the expression machinery's own warm-up).

Everything is id-correlated (`control.ts` `sendOp`), an unsolicited
`op` without an id goes to `onPush()` subscribers, and no op is
retried: offline is reported, not queued.

### 6.2 The ops

| op                 | request                                | reply                                                    |
|--------------------|----------------------------------------|----------------------------------------------------------|
| `omni.catalog`     | `list` (`commands`/`params`), `session`, `version` | `session`, `version`, `full`, `add` rows, `remove` keys |
| `omni.rows`        | `list`, `keys`                         | `rows`: key -> `{value, set}` or `{active}`              |
| `omni.objects`     | `doc` (optional)                       | `doc`, `label`, `objects[{name,label,type,children}]`, `views[{name,title,served}]` |
| `omni.resolve`     | `query`, `doc` (optional)              | `kind` `object` (`doc`,`obj`,`top`,`sub`,`label`,`type`) or `property` (`doc`,`obj`,`scope`,`view`,`prop`) |
| `command.run`      | `name`, `child` (optional row index)   | ok; `Forbidden`, `Inactive`, `UnknownCommand`, `NotGroup`, `CommandFailed` |
| `command.children` | `name`                                 | `exclusive`, `items[{index,text,command,tooltip,checkable,checked,enabled,visible,separator}]` (`command`: what the row runs, absent when unnamed) |
| `param.get`/`set`/`reset` | `key` (`ParamInfo::fullPath()`), `value` for set | `key`, `value`, `set`                        |
| `omni.changed` (push) | --                                  | `list`, `session`, `version`                             |

A command row is `{name, title, desc, shortcut, group}`; a parameter row
is the desktop row plus the editor's recipe -- `{key, path, group, entry,
name, title, doc, type, default, proxy, min, max, step, decimals,
transparency, items[{text,tooltip,data}], dataIsString}` -- so the page
builds the same choice of control `createParamEditor()` does: a select
over the items (value the index, or the item data), a colour input for
`Color` and every `Hex`, a number input with the spin box's range, a
checkbox, a text field for the rest. Values travel in
`ParamRegistry::getValue()` text form.

`command.run` is mutating and refused on a view-only connection like
`setProperty`. `param.set` and `param.reset` change the host's preferences for
everyone the process serves, so they need a host connection
(docs/ShareAccess.md sec 2.2) and an editing one is answered `Forbidden`
(`OmniControl::requiredAccess`).

`getProperties` and `setProperty` with subject/target `view3d` take an
optional `view`, a persistent view name, for `#.View2.DrawStyle`; without
it they address the served view, which is also what `#.ActiveView.` means
to a viewer -- the view it looks at, whichever MDI view the desktop has
active. For a served document that is the serving container, the one
whose `Render_*` knobs the inspector's "View" half shows, and it has no
`DrawStyle`: `#.ActiveView.Render_AO` resolves there, `#.View1.DrawStyle`
reaches the desktop's MDI view by name. `omni.objects` marks a named view
`served` when it is that container, and `omni.resolve` leaves `view` out
when the named view is the served one.

### 6.3 Where the mirror differs from the desktop box

- Enter on an object opens a card of that object's properties with
  editors, in the box, instead of the tree's hierarchy menu: the viewer
  has no tree, and the card is what a remote user can act on.
- A leading `.` is the viewer's own selection (`fc:selection`): the
  first selected object's properties are listed, and a commit issues one
  `setProperty` per selected object -- the backend has no transaction
  spanning them, and a failure on one is reported without undoing the
  others.
- A group command's arrow (or Right on the row) asks `command.children`
  and shows the rows as a menu; a row runs `command.run` with its index,
  which triggers the same `QAction` the desktop menu would.
- No `/param` mode (removed 2026-09-14): the host's preferences are not a
  browser's to change, whatever its level, so the box does not offer them;
  `param.set`/`param.reset` on the wire need a host connection
  (docs/ShareAccess.md sec 2.2). And a command a connection may not run --
  off the browser allowlist, unless it is a host -- is drawn refused and not
  sent.
- No icons, no `<<label>>` rows: the label is the object row's
  description, and `<<Label>>` still resolves when typed.
- **The box opens in OBJECT mode**, its line already holding `/ `, where
  the desktop opens on the chooser with a bare `/` (changed 2026-09-21, on
  the user's call, after a handset found the reason). `parseInput` reads a
  lone `/` as the chooser, so typing a name straight into it -- the
  obvious thing to do, and on a phone the only thing, there being no
  keyboard shortcut to reach any other mode -- reads as `/Box`: a chooser
  query that matches no mode row and lists nothing at all, with no hint
  that a space was wanted. The chooser is still one backspace away. The
  cost is the object mode's single `omni.objects` fetch (162 bytes, 13 ms
  measured) at opening rather than one keystroke later.
- ~~Building the web layer on the Windows box needs WSL and a
  wasm-rollup swap, and the page can only be exercised on the Linux
  box.~~ **Superseded 2026-09-12**: emsdk is installed on the Windows
  box, the WASM viewer builds there, and the DOM bundle builds natively
  with emsdk's own node -- native rollup loads, so no WSL and no
  `@rollup/wasm-node`. The recipe is
  [DevEnvironment.md](./DevEnvironment.md), "The WASM viewer on
  Windows". The box has now been driven in a real browser against a
  serving desktop: `/` opens it, the two catalogs arrive whole once and
  answer "current" on the next ask, and objects, commands, parameters,
  `Box.` members and `Doc#.` document properties all answer with no page
  errors. A plain-socket client is still the way to test the ops
  themselves. One trap for such a client: the
  server recognises the hello, `resync`, `docs` and `switch` verbs by
  the exact substring `"cmd":"hello"` -- a JSON encoder that puts a
  space after the colon is not a viewer, gets no pushes, and the ops
  still answer, so nothing says why.

**The write side, driven 2026-09-12.** Everything the mirror can change
has now been exercised from a real browser against a serving desktop,
with a probe in the serving process reading each effect back rather than
the page being believed:

| what | how it was driven | what the serving process saw |
|------|-------------------|------------------------------|
| a property edit from the box | `Box.Height`, Enter, the panel's number editor | 10 -> 25, and the document recomputed behind it |
| the object card | `Cylinder`, Enter, three of its editors | `Radius` 2 -> 7 (Quantity), `Label` -> a new string, `Visibility` -> false (a view-scope Bool) |
| `command.run` | `/cmd fit all`, click the row | the box closed, which is how a viewer sees an ok -- `run()` closes on the reply |
| a group command's child | `/cmd draw style`, the row's arrow, then Wireframe | the menu came back with its radio tick on As Is; the DESKTOP view's draw style became Wireframe |
| `param.set` / `param.reset` | `/param sync selection`, the checkbox, then Reset | stored false, then the stored value removed and the default back; the Reset button's own disabled state tracks whether anything is stored |
| the leading-dot selection | two objects selected, `.Height`, one commit | BOTH objects went to 33 -- one `setProperty` each |

The `/param` row records a mode the browser box no longer offers (sec 6.3).

The **Hex colour editor**, the corner these notes called the one most
likely to be wrong, is right: the picker's `#123456` is stored as
`0x123456FF` -- the alpha byte the picker has none of is preserved --
and Reset puts back `0xCCCCE6FF`. And the documented failure of a mixed
selection behaves as documented: `.Length` over a box and a cylinder
sets the box and reports `Failed: UnknownProperty`, with the box's edit
standing.

One thing a headless browser cannot be made to do here is the PICK
itself: under swiftshader a populated scene starves the main thread, so
the selection was synthesized by dispatching the same `fc:selection`
CustomEvent the wasm module dispatches (`wasm/main.cpp`,
`fcviewer_selection_event`). Everything after that -- the resolve, the
panel, the per-object commits -- was the page's own code.

### 6.4 What a connection may reach

Added 2026-09-12. Every op of the mirror that names a document -- and
`getProperties`/`setProperty` with it -- resolves that name through one
gate, `SceneControlDetail::documentAllowed()` in `SceneControl.cpp`. A
connection may address:

- **the document it is joined to**: the one its group serves
  (`boundDoc`, docs/MultiDocServe.md sec 3), or the active document when
  the handler is the desktop's unbound one;
- **the documents that one links out to, transitively** -- the external
  objects its own scene already shows, so inspecting a linked part in
  the browser keeps working.

Nothing else the process has open. That matters because the omni
grammar crosses documents by design: `Other#.Comment` is a member of
another document and `Other#Box.Length` parses through
`ObjectIdentifier`, so before the gate a viewer joined to one served
document could read -- and with `setProperty` write -- every other
document in the same process, which is the whole premise of serving
several documents to several people from one backend
(docs/MultiDocServe.md sec 5.1 and sec 7). The check is on the **result** of the
resolution, not only on the `doc` field, because the grammar reaches
places the request never named.

Two details worth keeping:

- **The refusal is indistinguishable from "there is no such document"**
  -- `UnknownDocument`, or `NoMatch` from `omni.resolve`. A separate
  `Forbidden` would turn the channel into an oracle for what else the
  backend has open, which is exactly what a shared-document host is
  entitled not to publish.
- **Reach is walked over the objects' own out-lists**, not
  `PropertyXLink::getDocumentOutList()`, which
  `Document::getDependentDocuments()` is built on. That map is keyed on the
  target document's **file name**, so with either document unsaved it
  knows nothing -- and a link is least saved when it is newest. The
  out-lists are cached on the objects, so the walk is over pointers and
  costs nothing worth measuring; it runs only when a request names a
  document other than the home one.

The in-list is deliberately not followed: that another document links
*into* this one says nothing about whether this connection may read it.

**What the gate is not.** It scopes a connection to the document it is
*currently joined to*, and a viewer may re-join: `switch` moves it to
any **served** document, which is by design and is the choke point
`joinDocument()` already names as where a per-connection ACL would go
(docs/MultiDocServe.md sec 4). So the gate keeps a viewer out of every
document the process merely has **open**; keeping one viewer out of
another *served* document is the grant work of docs/ShareAccess.md, not
this.

Verified over the live socket (2026-09-12) with a backend serving
`Served` while `Secret` and `Library` were also open, `Served` holding
an `App::Link` into `Library`, all three unsaved. `omni.objects`,
`getProperties`, `setProperty` and both resolve forms named `Secret`
and were refused with the answer a name belonging to no document gets;
the same ops named `Library` and answered. `tests/src/Gui/OmniControl.cpp`
`test_documentReach` is the same shape without a socket.

**Extended 2026-09-14** to the ops that act on a document they name
outside the mirror: `edit`, `resetEdit`, `command`, `onViewFocus`,
`undo`, `redo`, and Spreadsheet's `sheet.list`/`sheet.get`/`sheet.set`,
the last through `Gui::sceneControlDocument()`, the exported form of the
same resolution for a module's handler. They used to look the name up
directly, so an editing connection could undo, enter or leave an edit
mode, or write cells in any document the process had open -- and
`sheet.list`/`sheet.get`, being reads, answered a view-only connection
too. `test_documentReachOfEditOps` covers the first six.

**Still open, and the gate does not close it:** `command.run` and the
parameter ops are not document-addressed. A command runs against
whatever the desktop's own command layer considers active -- its active
document and its selection -- and `param.set` writes a process-wide
preference. So an *editing* connection joined to one served document can
still act outside it through those two ops. Both are refused on a
view-only connection, and since 2026-09-14 both need more of an editing one
(docs/ShareAccess.md sec 2.2): the parameter writes need a host connection, and
`command.run` holds a connection that is not a host to the browser allowlist the
`command` op keeps, judged on what will run -- a group row by its member -- and
answered `Forbidden` ahead of `Inactive` (an ordering of error codes only: whether a
command is active is already public through `omni.rows` and `command.children`). A
group's member rows carry the `command` they run, so the browser draws a refused one
disabled; the server still decides. So the hazards below are
a host's, who is the desktop's owner. Scoping them properly means either making the
command layer take a document (it takes none) or activating the served
document around the call (which moves the desktop user's focus), and
neither is a change to make in passing.
