# Omni search -- `/` over objects, commands and parameters

Status: implemented on the desktop (2026-09-11). The browser mirror is planned;
section 6 says what it will reuse.

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
would, with `Doc#` as the title; `#.ActiveView.DrawStyle` is a property of
the document's active 3D view (the `View3DInventor` is a property container
of its own, with `DrawStyle`, `ShadingType`, `ShowNaviCube` and so on). After
`#.` or `#.ActiveView.` the popup lists that container's properties -- name
and documentation, hidden ones left out -- plus an `ActiveView.` row under a
document that has a view; picking it opens the view's list. This popup is
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
  (`ObjectMatch::doc` set, `obj` empty), `#.ActiveView.X` on its active
  view, and a leading `#` before an object name is dropped. The owner
  comes from `TreeWidget::startItemSearch()`, which also sets up the tree's
  search state.
- `documentMembers(head, owner)` and `splitMemberQuery(query, head, tail)`:
  the rows behind a `#.` or `#.ActiveView.` popup, and the split of a query
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
(`completeMember()`), except that picking the `ActiveView.` row re-runs
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
- Commands need a `Gui::Application`; they are exercised by hand: `/cmd
  draw` -> `Std_DrawStyle` with its arrow, `/cmd history` -> Enter runs
  `Std_CmdHistory` and it appears in the history.

## 6. The browser mirror (planned)

The viewer's control channel (`SceneControl.cpp`, `web/src/control.ts`
`sendOp`, see `ThinClient.md` section 4.2) speaks id-correlated JSON ops and
today knows `getProperties`, `setProperty` and the `cycles` ops. The mirror
adds ops that call the same layer the box does, which is why that layer has
no widgets in it:

| op                 | fields                    | calls                                        |
|--------------------|---------------------------|----------------------------------------------|
| `omni.search`      | `mode`, `query`           | `searchCommands`, `searchParams`, or the expression completer's model for objects |
| `omni.resolve`     | `query`, `doc`            | `resolveObject` -> object/sub-object/property, or a document/view property (`#.`) |
| `omni.members`      | `head`                    | `documentMembers` for a `#.` or `#.ActiveView.` head |
| `command.run`      | `name`                    | `CommandManager::runCommandByName`           |
| `param.get/set/reset` | `path`, `entry`, `value` | `ParamRegistry::getValue`/`setValue`/`reset` |

`command.run` and `param.set` are mutating and refused on a view-only
connection like `setProperty` is. The property editor of a resolved property
is the browser inspector's existing `setProperty`. The parameter editor
needs `ParamInfo` on the wire -- type, proxy, items, range -- which
`omni.search` returns per row, so the page can build the same choice of
control. The command list carries `group` per row; expanding one needs a
`command.children` op over `ActionGroup::actions()`. None of this is
implemented yet.
