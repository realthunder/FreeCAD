# The expression editor

Every expression bound in a document, as one text you can edit, diff and
apply. It is the counterpart of the copy entries of `Std_Expressions`
(Edit menu, "Expression actions"): where "Copy selected", "Copy active
document" and "Copy all documents" put the bindings on the clipboard, "Edit
selected...", "Edit active document..." and "Edit all documents..." open
them in an editor window. Those three are commands too --
`Std_ExpressionEditSelected`, `Std_ExpressionEditDocument` and
`Std_ExpressionEditAll` -- so they can take a shortcut or sit on a tool bar.

Code: `src/Gui/ExpressionEditorView.{h,cpp}` (the text form, the diff and the
view), `src/Gui/ExpressionSyntaxHighlighter.{h,cpp}`, the commands in
`src/Gui/CommandDoc.cpp`. Tests: `tests/src/Gui/ExpressionEditor.cpp`
(`ExpressionEditor_tests_run`).


## 1. The text

The copy format, unchanged, so a text copied to the clipboard pastes into the
editor and back. Each binding is a block:

    ##@@ .Length Unnamed#Box.ExpressionEngine (Box)
    ##@@<comment>
    Width * 2

The first header line names the path, the object and the property (the label
in parentheses is for the reader); the second carries the expression's
comment, `&`-encoded when it has a newline. The body runs to the next header.
A body that is a lone `#` unbinds the property.

Text before the first header, and a `##@@ ` line that names no binding, are
errors: Apply refuses the text and puts the cursor on the line.


## 2. The tool bar

The "Expression editor" tool bar is in every workbench but shows only while an
expression editor is the active view. Every button is a command, so each can
also get a shortcut or go on another tool bar.

| Command | Does |
|---|---|
| `Std_ExpressionApply` | Binds what the text says, in one transaction ("Edit expressions", undone by Std_Undo from a document view). Every body is parsed first; if one fails nothing changes. Bindings that would not change are left alone. The text is then reloaded, so it reads as the documents now hold it. A body's trailing blank lines are not part of the expression, so an untouched block never counts as a change. |
| `Std_ExpressionDiff` | Toggles between the text and a line diff of it against the text last loaded (added lines green, removed red). |
| `Std_ExpressionRevert` | Puts back the text last loaded. Ctrl+Z brings the edit back. |
| `Std_ExpressionRefresh` | Reloads from the documents -- for changes made elsewhere since. Asks first when the text has unapplied edits. |
| `Std_ExpressionUnbind` | Turns the block at the cursor, or every block the selection touches, into an unbinding block (`#`). Nothing changes in the document until Apply. |

"Last loaded" is the last open, Apply or Refresh.

**Deleting a block** is also an unbind request, but a deliberate one is easy to
tell from an accident only by asking: on Apply, when blocks of the loaded text
are missing, one question covers all of them -- Unbind, Keep bound, or
Cancel.

Closing an editor with unapplied edits asks Apply / Discard / Cancel.


## 3. Folding

Each block folds under its first header line: click the arrow in the line
number margin. A folded block shows `...` after its header. Moving the cursor
into a folded block unfolds it. Folds survive Apply, Revert, Refresh and
Unbind, matched by the header line's text -- a renamed label unfolds its
block.


## 4. Where the window opens

Where any other non-3D document view opens (docs/ViewPlacement.md): tab,
split or floating, per the preference, with the Alt inversion. One exception:
the split's reuse step never replaces the view of an object the editor
covers, so editing a spreadsheet's expressions does not close the spreadsheet
(ViewPlacement.md 3.2 step 6, the `keep` predicate). Asking for an editor that
is already open brings that one forward.

An editor on the selection or the active document belongs to that document
and closes with it; an editor on all documents belongs to none.


## 5. Highlighting

`ExpressionSyntaxHighlighter` follows the lexer (`src/App/ExpressionParser.l`)
rather than Python, and uses the Editor preference colors:

- `#` starts a comment only before a blank or the end of the line; `Doc#Obj`
  is a reference.
- `#@pybegin` ... `#@pyend` switches to Python rules, where every `#` is a
  comment.
- `<<...>>` is a string; triple-quoted strings span lines.
- A number carries the unit written against it: `5mm`, `360deg`.
- Keywords are the lexer's (no `class`, `with`, `yield`); a builtin function
  called by name (`cos(`) takes the class-name color, a `def` name the
  define-name color.
- The `##@@` header lines are drawn as headers.

The same highlighter colors the expression dialog (`ExpressionTextEdit`, also
the spreadsheet cell editor), and a text document whose "Syntax highlighter"
view property is `Expression` -- the default for an `App::ExpressionLibrary`.
