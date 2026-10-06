# Hands-on queue

Problems found by running the program by hand, in the order they were
reported, worked in turn. One entry per problem: what was seen (the reporter's
words where there are any), what it turned out to be, and the commit that
closed it. The copy under test is the staged one -- `docs/DevEnvironment.md`,
"A second env for hands-on testing" -- so a fix reaches the reporter only at
the next stage, and each entry says which stage has it.

Stages so far: 2026-10-06 07:56 (`84c14e12d5`, the first), 2026-10-06 11:23
(`c1028260e3`: entries 1, 2, 4, 7), 2026-10-06 13:59 (`489c64799c`: entries 3, 5, 6, and
the helix of entry 8), 2026-10-06 14:44 (`6b1bd3f434`: entry 14).

States: `OPEN` (not looked at), `FOUND` (cause known, no fix yet), `FIXED`
(committed and tested in the dev tree, not staged yet), `STAGED` (in the copy
under test, waiting for the reporter to confirm), `CLOSED`.

Evidence that does not belong in the repository -- configuration snapshots,
report views, the reporter's own files -- is kept beside the dev tree under
`..\dl\handson\<date>\`, and an entry names what it holds.

| # | Reported | Problem | State |
|---|---|---|---|
| 1 | 2026-10-06 | idle progress bar in the status bar | STAGED |
| 2 | 2026-10-06 | a file opened from the menu comes up empty (`scanner.FCStd`) | STAGED |
| 3 | 2026-10-06 | tooltips are clipped: navigation style, and toolbar buttons with an icon | STAGED |
| 4 | 2026-10-06 | status bar dimension reads `100 mm x 80 mm`, wanted `100 x 80 mm` | STAGED |
| 5 | 2026-10-06 | title bar with the workbench bar docked: the menu does not unfold on hover | STAGED if it is entry 6's cause; to confirm |
| 6 | 2026-10-06 | maximized with the custom title bar: sometimes no margin at the top | STAGED |
| 7 | 2026-10-06 | crash after answering Yes to the recompute question on `scanner.FCStd` | STAGED, cause of the GL error open |
| 8 | 2026-10-06 | `scanner.FCStd`: the migration recompute fails | FOUND in full; the helix STAGED; the rest is entries 14 to 16 |
| 9 | 2026-10-06 | the 3D view lags behind the mouse: hover highlight, wheel zoom | OPEN |
| 10 | 2026-10-06 | a 3D view is slow to take a new size | OPEN |
| 11 | 2026-10-06 | dark theme: wrong colors (checkbox border, title bar buttons), audit asked | OPEN |
| 12 | 2026-10-06 | TechDraw: dimensions and cosmetics are covered by the face fill | OPEN |
| 13 | 2026-10-06 | report view: grouped messages with an expand icon in the margin, no underscore (change request) | OPEN |
| 14 | 2026-10-06 | a Draft with no neutral plane given turns the other way after a recompute (from entry 8) | STAGED |
| 15 | 2026-10-06 | a Pad "up to first" gives a third result (from entry 8) | OPEN |
| 16 | 2026-10-06 | faces of a "Mutated" copy-on-change binder are renamed by every recompute in a new session (from entry 8; the old build too) | FIXED |
| 17 | 2026-10-06 | `Sketch043`, `Sketch055`: "Missing external geometry reference", seen once the binders of entry 16 are valid | OPEN |
| 18 | 2026-10-06 | TechDraw pages do not load: "invalid vector subscript", the views loose in the tree, 320 objects restored to defaults | FIXED |
| 19 | 2026-10-06 | TechDraw: other indexes taken on trust (an audit asked) | OPEN |

## 1. Idle progress bar in the status bar -- STAGED

**Reported (2026-10-06):** "no activity but the status bar progress bar is
shown with 0 progress."

**Seen in the running copy:** `Gui::ProgressBar` visible, value -1 (reset), no
document open, no sequence running.

**Cause:** the status bar's item registry (`MainWindow::relayoutStatusBar`)
shows every widget that does not carry a `userEnabled` property, and restores
the on-screen state of the ones that do. The registry was ported from upstream
without the property on the progress bar, so every registration -- start-up
included -- showed it.

**Fix:** `e511bddc39`. `tests/gui/statusbar-progress-idle.py`, 11 PASS (4 FAIL
before).

## 2. A file opened from the menu comes up empty -- STAGED

**Reported (2026-10-06):** "with current setting (take a snapshot of user.cfg)
opening d:\Zheng.Lei\mech\scanner.FCStd got a bunch of warning and errors in
console without showing anything in the tree or the 3d view."

**Evidence:** `..\dl\handson\2026-10-06\` -- `user.cfg.ondisk` and
`system.cfg.ondisk` (the files as they were on disk at 09:37),
`user-BaseApp.live.cfg` (the running program's parameters, exported),
`report-view-full.log` (the report view), `scanner.FCStd` (a copy of the file,
677 objects, written 09:17 by FreeCAD-Link 2025.1020).

**Seen in the running copy:** the document `scanner` is open with 0 objects.
The report view is one message per object of the file, 677 of them: `Cannot
create object 'Dimension097': (The document 'scanner' is still being filled
in, and a command may not change it until that finishes. ...)`.

WARNING -- an empty `scanner` document is open in that session with the file's
path. Saving it would write an empty document over the file. The copy above is
intact.

**Not the settings.** Reproduced in the dev tree by `Std_RecentFiles` with the
reporter's `user.cfg` and again with a `user.cfg` holding nothing but the
recent file: 677 refusals and 0 objects both times.

**Cause:** File > Open and the recent list are commands, and a command runs
inside `App::Document::UserEditGuard`, which refuses any change to a document
carrying `LiveImport`. A load sets `LiveImport` on its own document
(`Gui::Application::refreshLiveLoad`) so that a command clicked meanwhile is
refused -- and the load a command started runs under that command's guard.
Both halves date from 2026-08-23 (`fa503839a2`, `e20bedc723`) and neither has
changed since; every test opens its files from Python, a command line argument
or a drop, where no guard stands, which is how it went unseen.

**Fix:** `c988990274`. `App::Document::UserEditSuspend`, held by
`Application::openDocuments` and `Document::restore` -- it steps the guard down
for the load and a command clicked meanwhile raises its own inside it.
`DocumentTest.liveImportUserEditSuspendedForTheLoadItself`,
`tests/gui/open-through-command.py`, 6 PASS. The reporter's file through the
recent list afterwards: no refusal, the objects arrive and the migration
recompute runs (entry 8 is what it then does).

**Not checked yet:** an import started from the menu that turns `LiveImport`
on for itself (`Gui.setLiveImport`, the IFC importer) stands in the same place.

## 3. Tooltips are clipped -- STAGED

**Reported (2026-10-06):** "the tooltips of navigation style option in status
bar is clipped." Then: "not only the navigation tooltips, some of the toolbar
button tooltip with icon is also clipped. check my running instance."

**Seen in the running copy:** tooltips are drawn by `Gui::TipLabel`
(`Widgets.cpp`), not Qt's; the navigation style tips are rich text, a table
of `<img>` cells (`Mod/Tux/NavigationIndicatorGui.py`).

**Reproduced** in the dev tree with the reporter's configuration, by sending
each visible toolbar button and each navigation style the event a hover sends
and photographing the tip, both as the widget drew it and off the screen
(`..\dl\gt-tips-fc3\`, 45 tips). One condition first: `Gui::ToolTip` installs
itself on the application at its first use, which is the first preselection
in a 3D view -- before that the tips are Qt's and none of this shows. A fresh
start looks right; a session that has touched a model does not.

**Two defects**, neither of them the screen or the stylesheet (each tip on
screen matched what the widget drew, pixel for pixel):

- A tip of two lines is lower than its icon. The label was sized to the
  icon's height alone, and the icon is drawn inside the label's margin: 64 px
  of picture in a 64 px label starting 2 px down. `Std_MeasureDistance` and
  `PartDesign_SubShapeBinder` of the first 40 buttons; any command with a one
  line tip.
- The icon is found as the FIRST `<img>` of a tip, whoever put it there. In a
  navigation style's tip that is the first cell of the table of mouse buttons:
  the "Select" cell came up empty and its picture hung in the top right
  corner, in all eleven styles. `Mod/Material`'s tree tips carry pictures the
  same way.

Checked and NOT a defect: an icon that looks cut at the right or the bottom
edge (`Std_CloseAllWindows`) is the artwork; the pixmap in the tip is byte for
byte what the SVG renders to at 64 px.

**Fix:** `b1ba3ecc0d`. The height takes the margins in; only an image
floated right -- what `Action::createToolTip()` writes -- is taken for the
icon. `tests/gui/tooltip-icon-and-images.py`, 6 PASS over 58 toolbar tips and
11 navigation styles.

## 4. Status bar dimension: `100 x 80 mm` -- STAGED

**Reported (2026-10-06):** "in the status bar the dimension, instead of
something like 100 mm x 80 mm, write it as 100 x 80 mm."

**Where:** `View3DInventorViewer::printDimension()` joins two strings that
each carry their unit.

**Fix:** `15927772df`. The unit is said once when both sides share it
(`176.99 x 80.00 mm`); sides in different units keep both (`15.49 m x 7000.00
mm`), and so does a schema whose text does not end in its unit.
`tests/gui/status-dimension-text.py`, 3 PASS.

## 5. Title bar with the workbench bar docked: the menu does not unfold -- FIXED if it is entry 6's cause; to confirm

**Reported (2026-10-06):** "in the customized titlebar is docked with
workbench sometimes malfunction, the menu bar will not show when mouser hover.
I can fix it by re-docking the workbench bar." Then: "the customized title bar
problem seem to happen before, check git history."

**Seen in the running copy (state at the time, not known to be the failing
one):** `TitleBarWidget` 1920 x 35 at the top of the main window;
`FoldableMenuBar` 54 x 35 holding a `QMenuBar` of 558 x 21; the `Workbench`
toolbar (851 x 35, `WorkbenchTabWidget` 832 x 29) parented to
`MenuBarLeftArea`, not to the main window.

**Not reproduced as reported.** With the reporter's configuration in the dev
tree the title bar comes up as above and the hover mechanism is intact: the
logo button is on top at its own centre, a 250 ms timer on it unfolds the bar
(`TitleBarMenuButton`, `MainWindow.cpp`), the overlay is raised on every
unfold. Nothing in it depends on the workbench bar.

**What entry 6 found does explain it:** in the state a run-time switch of the
title bar leaves a maximized window in, Qt maps the pointer 8 px away from
where the widgets are drawn, so a pointer resting on the logo is, to Qt, not
on it. That state lasts until the window leaves the maximized state. Whether
re-docking the workbench bar does anything about it was not established, so
this is closed only if the reporter no longer sees it after the stage that
has entry 6's fix.

## 6. Maximized with the custom title bar: sometimes no margin at the top -- STAGED

**Reported (2026-10-06):** "when maximized, sometimes, with the customized
toolbar, it leaves no margin at top. sometimes it is fine."

**Seen in the running copy (maximized, looking right at the time):** window
geometry (0, 0, 1920 x 1040), frame (-8, -8, 1936 x 1056), screen 1920 x 1080
at 100%, title bar at y = 0.

**"Sometimes" is: after the title bar was switched while the window was
maximized.** Started with the custom title bar on, the window is right however
it is then maximized (Qt, the system, back from minimized, back from full
screen). Switched at run time -- `CustomTitleBar` written by a theme or a
preference pack, or Std_ViewTitleBar -- on a maximized window, it is not. The
reporter's configuration had no `CustomTitleBar` at 09:25 and had it at 09:37,
with a preference pack applied at 09:33.

**Measured** (`..\dl\gt-maxprobe-2`, `-3`, `-4`, `gt-normalprobe-*`), 1920 x
1080 screen at 100%:

| | the system's client area | Qt's geometry |
|---|---|---|
| started custom, maximized | (0, 0) 1920 x 1040 | (0, 0) 1920 x 1040 |
| switched on while maximized | (0, 0) 1920 x 1040 | (-8, 8) 1936 x 1040 |
| back to normal after that | (0, -9) 1920 x 1018 | (0, -8) 1920 x 1017 |
| after a second off and on | (-16, -40) | (-16, -39) |

So: 16 px of title bar past the right edge with the window buttons, every
widget answering the pointer 8 px from where it is drawn, and a normal
placement above the top of the screen, further with every switch.
`SWP_FRAMECHANGED` does not reconcile the two accounts and neither does a
resize; leaving the maximized state does.

**Fix:** `43b51e8425`. `MainWindow::applyTitleBarParams()` takes a maximized
window out of that state, switches a turn of the event loop later, and
maximizes it again; Std_ViewTitleBar goes through the parameter and takes the
same road. Windows only. `tests/gui/titlebar-switch-maximized.py`, 7 PASS.
Tried first inside the vendored kit's `attach()`/`detach()` and not kept: Qt
has to hear of the restore through its window system queue before the flags
change, which from inside the backend means running the event loop there.

**Still to look at here:** the earlier fixes this builds on (`8a7f412fe3`,
`d712eae660`) are in the kit as LOCAL DIVERGENCE; this one is not in the kit,
so a program using the kit without FreeCAD's main window still has it.

## 7. Crash after answering Yes to the recompute question -- STAGED, cause of the GL error open

**Reported (2026-10-06):** "a crash just happend. check the minidump." Then:
"recompute request dialog is poped. last time crash happend after I said yes",
and: "last time I said yes before the document is fully loaded. maybe that's
also a factor."

**Evidence:** `..\dl\handson\2026-10-06\` -- `crash1-cdb.log` (the debugger's
log: the first-chance stack, 86 frames with lines), `crash1-crash.log`,
`crash1-report-view.log`. The full dump is
`..\tools\dbg\dumps\fcad_user_av_1c84_2026-10-06_10-33-26-440_ffb8.dmp`,
6.9 GB; nothing below needed it.

**What happened, from the stack:** the file was opened from the Python
console (`FreeCADGui.loadFile`), the "Recomputation required" question was
answered Yes, the recompute failed ("Recompute failed!"), and the "Recompute
error" box opened. That box runs an event loop; the 3D view repainted inside
it; `BGFXView::blitReadback` ended with `checkGLError("readback composite")`;
OpenGL had an error to report; and reporting it crashed: access violation in
`QDebug::operator<<(const char*)`, reading address 0x2.

**Cause of the crash:** `_checkGLError` named the error from a table of four
strings indexed with `min(code - GL_INVALID_ENUM, 4)`. Three codes have a
name, 0x0503 got "Unknown", and everything from `GL_STACK_UNDERFLOW` (0x0504)
up -- out of memory, invalid framebuffer operation -- indexed entry 4, one
past the end. Same code in the Diligent backend.

**Fix:** `41f6c2cdcb`. `Render::glErrorName()` in
`src/Gui/Renderer/GLErrorName.h`, total over every value, used by both
backends; the line now carries the code in hex. `GLErrorName_tests_run`.

**Still open here:** which GL error it was, and why the readback composite
raises one while a message box is up. The fixed line will say. The repeat in
the dev tree -- same file, opened through the recent list, Yes answered once
the document had finished loading -- reached the same "Recompute error" box
and repainted without a GL error, so answering before the load had finished
is the lead to follow.

## 8. `scanner.FCStd`: the migration recompute fails -- FOUND in full; the helix FIXED

**Seen (2026-10-06), twice:** the file is from FreeCAD-Link 2025.1020 and asks
for a recompute "for migration purpose"; the recompute ends in "Recompute
failed!".

**Measured against the build that wrote the file.** The same script was run
headless in both -- `FreeCAD-Link-Tip-...-20251015\bin\FreeCADCmd.exe` (OCCT
7.7.2) and the dev tree (OCCT 8.0.1) -- on a copy of the file: touch every
object, recompute, list what is invalid and every object's volume
(`..\dl\handson\2026-10-06\entry8-*.txt`). The old build needs no recompute to
open the file (0 objects touched as loaded; here 223), so nothing below shows
in it until something is recomputed.

| | old build, full recompute | this build |
|---|---|---|
| invalid | 4: Binder013, 014, 017, 018 | 6: the same four, `Draft`, `Fillet011` |
| `Helix`, `Helix001` (subtractive) | 978.14, as saved | 17.49 |
| `Pad051` and the four features after it | 3400.82 ... | 3282.67 ... (saved: 3256.82) |

Four separate things:

1. **The subtractive helix cut nothing and kept the intersection -- FIXED,**
   `a0a7f68092`. The file has `Outside` on and `AddSubType` Subtractive, and
   the old build cuts: in this fork the type was already the authority and
   `Outside` was left over, doing nothing. `Helix::onDocumentRestored` (from
   `fda7e0a6ce`, 2026-09-27) read that pair as a file from before
   `AddSubType` and turned it into Intersecting. It does that now only for
   an object without `_ProfileBasedVersion`, which is what an upstream file
   is. `TestHelix.testSubtractiveOutside` covers both kinds of file; the two
   helixes and the two links to them come out as saved again.
2. **`Draft` fails** ("Failed to create draft:"), and `Fillet011` after it
   only in the sense of the next entry. Entry 14.
3. **`Pad051` and what follows it** (`Hole007`, `Pocket040` to `042`, and
   `Fillet011` on `Pocket042`) differ. Entry 15.
4. **The four binders.** Not a regression: the old build breaks them the same
   way as soon as `Binder008` is recomputed. Entry 16.

## 9. The 3D view lags behind the mouse -- OPEN

**Reported (2026-10-06 11:58):** "the 3d view seems lagging in response to
mouse movement and wheel. a mouse over highlight is visibly delayed a few
hundries of ms. and moving away the mouse to an empty area does not cancel the
previous highlight. mouse wheel zoom is also visibly lagging. after stopping
wheeling, I can see the 3d view is catching up with the lingering zooming
operation."

Three symptoms: (a) the preselection highlight arrives a few hundred ms late;
(b) moving to empty space does not clear it; (c) wheel zoom queues up and
keeps playing after the wheel has stopped.

**Added by the reporter:** 12:01 "scanner.FCStd was open when it lagged";
12:04 "a document with just a box behave the same. there seem to have a
general delay in renderer response that's cause the problem."; 12:06 "there is
no visible mouse hover highlight or wheel zooming delay in Techdraw view", and
"it is draw by the same renderer backend". So: not the size of the model, and
the same backend draws a TechDraw page without the delay -- what differs
between the two kinds of view is where to look.

**Ruled out so far:** the debugger the copy runs under. Its log has 321
first-chance C++ exceptions for the 33 minutes of that session, not one per
mouse move.

## 10. A 3D view is slow to take a new size -- OPEN

**Reported (2026-10-06 12:04):** "when I create a new document, the mdi window
will zoom to fit. the background gradient is stuck at its old size and visibly
delayed almost a second and more to fit the window. same for switching the
view window, the background together with content stuck for too long to fit.
these may or may not be related to the mouse problem".

Two cases: (a) a new document's view growing to fill the MDI area -- the
background stays at the old size for a second or more; (b) switching between
view windows -- background and content both stay at the old size too long.
Possibly one delay behind this and entry 9, in the reporter's reading.

## 11. Dark theme: wrong colors -- OPEN

**Reported (2026-10-06 11:58):** "checkbox border and customized toolbar
maximize/minimize icon got bad color in dark theme. audit for other similar UI
color problem."

Two named: the checkbox border, and the maximize/minimize icons of the custom
title bar. Asked for beyond those: an audit of the dark theme for other
widgets with the same kind of wrong color.

## 12. TechDraw: dimensions and cosmetics are covered by the face fill -- OPEN

**Reported (2026-10-06 11:58):** "techdraw dimension/cosmetics is covered by
face filling."

## 13. Report view: grouped messages (a change request) -- OPEN

**Asked (2026-10-06 11:58):** "do not use underscore in console grouped
message, intead, put a clickable expansion icon before the message (note,
those grouped message should still align with other normal message, put the
icon in front of it, in the margin area)."

Wanted: no underscore on a grouped message; a clickable expand icon ahead of
it; the message text itself stays aligned with ordinary messages, the icon in
the margin.

## 14. A Draft with no neutral plane given turns the other way -- STAGED

**From entry 8.** `Draft` in `scanner.FCStd`: face `Face6` of `Pad036`, 11 deg,
`Reversed` on, no neutral plane and no pull direction. Old build: valid, 285.76.
This build: "Failed to create draft:", and with `Reversed` OFF the same 285.76.

**Not the kernel's draft.** On a plain box every case agrees between 7.7.2 and
8.0.1, guessed plane included (`entry8-box-*.txt`), and the pad's shape as
stored in the file, drafted on its own in this build, gives the old result.

**Cause:** with no neutral plane given, `Draft::execute` guesses one from an
edge of the first face: through the edge, its normal -- the pull direction --
along the face. Which edge, and which of the two ways along the face, decide
which way the draft goes, and both came out of how the shape happens to be
written down: the first edge that will do, in the order the face lists them,
and the cross product of the edge's own direction with the axis of the face's
surface. Recomputed here, `Pad036`'s top face has its edges in another order
AND its plane the other way up (`entry8-draft*.txt`, `entry14-record.txt`).
The order alone was the first reading and was wrong: with the same edge taken,
the draft still turned over.

**Decided (the reporter, 2026-10-06):** "1 yes" -- write the guessed edge down
by its mapped name, taken from the stored shape on restore, so old files keep
their result.

**Fix:** `73da015564`. `_NeutralEdge` holds the edge by its mapped name and
`_NeutralSense` the side, as it relates to the face (into the face from the
edge, or with the face's outward normal where the plane is across the face).
The first guess fills them in; a file from before them gets them from the
base's stored shape as it is restored. On the reporter's file the record
follows the edge from `Edge4` to `Edge10` across the pad's recompute and
`Draft` comes out valid at 285.76 with `Reversed` on, as saved.
`TestDraft.testGuessedNeutralPlaneKeepsItsEdge`; TestDraft 4 OK.

## 15. A Pad "up to first" gives a third result -- OPEN

**From entry 8.** `Pad051`: `Type` UpToFirst, `Reversed`, profile `Binder033`,
base `Pocket039`. Volume as saved 3256.82; old build recomputed 3400.82; this
build 3282.67. The base and the profile are the same in both builds by volume
and by face and edge count. The old build does not reproduce the saved value
either, so the file does not say which is right. `Hole007` and `Pocket040` to
`042` sit on it and differ in step; `Pocket040` and `042` are `TwoLengths` in
the old build and read here as `Length` with `SideType` "Two sides", with the
same tool volume where the base is the same (`Pocket042`: 577.27 in both).
Not looked at further.

## 16. Faces of a "Mutated" binder are renamed by every recompute in a new session -- FIXED

**From entry 8; the old build does the same.** `Binder008` binds `Body004`
with `BindCopyOnChange` Mutated. Such a binder copies its support into a
temporary document (`_tmp_binder`, `SubShapeBinder::update`), and the element
names of its shape carry the copies' object ids: `Face1` was
`...;:Hd4b:7,F;:Hd4c,F;...` in the file, `...;:H86d:7,F;:H86e,F;...` after a
recompute here and `...;:Hb88:7,F;:Hb89,F;...` after one in the old build. The
ids are whatever the temporary document's counter stood at, so they differ in
every session, and `Binder013`, `014`, `017` and `018`, which refer to
`Binder008`'s faces by those names, lose them: "Failed to obtain shape
scanner#Binder008.?Face1".

What makes it show here is that this build recomputes the file on opening,
for migration, where the old one had no reason to recompute `Binder008`.

**Asked (the reporter, 2026-10-06):** "check remote Transaction branch on its
importing of external document element names. see if it solves the binder
naming problem".

**It does not, read from the branch** (`origin/Transaction` at `7d2c9e0a23`;
not built or run here). What it does for a shape that crosses documents
(`docs/TransactionLog.md` 27.76 to 27.80; `1effdebb0e`, `eac207251f`,
`9f18e39c1b`):
- the STRING ids in the names (`#98`) are translated into the table of the
  document the shape arrives in, where they used to stay as the other
  table's numbers and mean nothing after a reopen;
- the external marker names the document it came from, `;:X#<id>`, and a
  shape with the old bare marker asks for its owner's recompute once;
- a reference holds the strings of the name it refers by.

A binder's copy lives in a temporary document, so its shape is such a
crossing, and `SubShapeBinder::update` takes all three. The part of the name
that changes here is none of them: it is the OBJECT ids of the copies
(`:Hd4b`, `:Hd4c`, `:Hd53`), and the design leaves those alone on purpose --
27.76 item 3, "Tags are not imported". Two things follow:
- the branch names the ORIGINAL's document in the marker for a copy ("a
  copy's is the original's, its own being temporary") while the tags to the
  left of it are still the temporary copies' ids, which is not what its own
  rule 3 says those tags are;
- it recomputes every crossing shape once for the new marker, which is the
  recompute that renames `Binder008`'s faces. A file like this one would
  lose the four references on its first open there, as it does here.

What the branch does show (27.74) is the case that IS stable: a
copy-on-change LINK keeps its copies as objects of the document, saved with
it, so their ids are the same in every session and "the instance's
references keep their names" through a recompute and a reopen.

**Three ways were put to the reporter:**
- B. Replace the copies' ids in the binder's names by those of the objects
  they are copies of.
- C. B, and a missing name is also looked up with the copies' ids taken out
  of both sides, so that once is repaired on open.
- E. Keep a Mutated binder's copies in the document, as a copy-on-change
  link does.

**Decided (the reporter, 2026-10-06):** "do B+C first then the rest of issues in
the notes. no Transaction merge for now."

**What checking B and C found, before anything was changed:**
- The three ids are the sketch's, the pad's and the body's copies, and
  `Document::copyObject` hands the copies back in the order of what they are
  copies of.
- The temporary document is ONE for the whole session: `newDocument(
  "_tmp_binder", ..., tempDoc)` returns the one that exists. Every Mutated
  binder copies into it. Its object ids start at random (`DocumentP`'s
  constructor) and go on from copy to copy.
- The STRING ids in the names (`#98`, `#e:1`) change for the same reason.
  The strings hold the object ids -- `#f = #d:;:H98,E`, a sketch edge's name
  with the sketch copy's id -- so another copy id is another string and gets
  another number: `#f`, then `#22`, then `#34` over three copies of one body.
  With those out as well, `Face1`, `Face2` and `Face3` of `Binder008` read the
  same, so C as it was put ("exactly one face matches") could not have worked
  on this file.
- The search by geometry, which repairs a renamed reference everywhere else,
  finds nothing here because the shape MOVES. `Binder008` is `Relative` and
  sits in `LinkGroup001`, whose placement is 53 mm along z; its stored shape
  was made with the group at the origin (`Cache_Body004` is the identity in
  the file). Recomputed, it is seen from the group and lands 53 mm away -- in
  the old build too. The search looked where the faces had been.

**Decided again (the reporter, 2026-10-06, on hearing the above):** "is it
because each document object id has a random start. you can reset object id
to fixed on for temp document".

**Fix.** Two changes, in `SubShapeBinder::update`:
- *The names.* Each Mutated binder copies into a temporary document of its
  own (`_tmp_binder_<document>_<binder>`), emptied before each copy:
  `Document::clearDocument()` starts the object ids over, and the string
  table is cleared with it. The copies are then numbered 1, 2, ... in the
  order of the dependency list and their strings are made in the same order
  every time. `Binder008`'s `Face1` is `#e:1;:G;XTR;:H2:7,F;:H3,F;:X;BND:-1:0;
  :Hb:12,F;:H-58b:1b,F` after a full recompute, and the same, character for
  character, after a save, a new session and another full recompute
  (`..\dl\handson\2026-10-06\entry16-first.txt`, `entry16-second.txt`).
- *The once.* A binder whose every support is seen from another place than
  at its last update, all by one motion, says so
  (`Part::Feature::setShapeMotion()`), and the generations of its shape
  retained at that change are searched moved the same way
  (`searchElementCache()`). On the reporter's file `Binder013`, `014`, `017`
  and `018` come out valid, on `Face1` and `Face3` as before, after the full
  recompute that used to lose them.

They come out 53 mm from where the file had them, with `Binder008`. That is
the file's own state -- a binder never recomputed since its group moved --
and not something either change does.

`TestSubShapeBinder.testCopyOnChangeNamesAreTheSameEveryTime` (the copies
numbered from 1, a second binder of the same body naming the faces alike, the
names the same after a reopen and a recompute of everything) and
`testMovedBinderIsSearchedWhereItWent`; TestShapeBinder 8 OK. The second has
not been scored against a tree without the motion yet.

**Left, and known:**
- Several changes of a copied property in one session make their strings in
  another order than a new session does, which copies and applies the
  properties once. The names then change once more after a reopen, and the
  search by geometry has to find the references.
- The names still say the COPIES, by numbers that mean nothing outside the
  temporary document. `origin/Transaction` has what B asked for: a map of
  tags applied while a shape's names are imported (`StringHasher::ImportTags`,
  used for a file imported as a branch). After the merge the binder's copy is
  one more user of it. Its `rewriteTags` replaces the digits and leaves the
  length fields that count across them: on the fixture here a shorter id
  changes `:15` to `:14` and `:21` to `:1f` in the same name, so that is to
  look at then.
- With the four binders valid, what is built on them is recomputed for the
  first time: `Sketch043` and `Sketch055` now say "Missing external geometry
  reference". Entry 17.

## 17. `Sketch043`, `Sketch055`: "Missing external geometry reference" -- OPEN

**From entry 16.** With `Binder013`, `014`, `017` and `018` valid again, what
is built on them is recomputed for the first time in a full recompute of
`scanner.FCStd`, and these two sketches fail (`entry16-first.txt`). Not looked
at. One thing to check first: the four binders move 53 mm with `Binder008`
and are renamed with it, and they are not the ones whose container moved, so
the search by geometry has no motion to go by for a reference into THEM.

## 18. TechDraw pages do not load: "invalid vector subscript" -- FIXED

**Reported (2026-10-06, the Inbox notes of 15:56 and 16:19, and then):**
"currently techdraw pages does not load correctly", "see why techdraw
grouping is in correct. probably due to progressive loading", and, on the
cause, "how could this happen. add code to clamp to prevent it from
happening again".

**Seen.** Opening `scanner.FCStd`: "deferred view provider restore aborted
(invalid vector subscript), 320 objects fall back to defaults"; the views of
a page loose in the tree; with the page drawn by the backend, a blank page
and Qt's "endPaint() called with active painter" without end. Saving from
such a session would have written default view properties over the file's.

**Cause.** The reporter's `user.cfg` had `Mod/TechDraw/Standards/LineStandard`
at -1. It is an index into the line standards found, and
`Preferences::lineStandard()` handed it out as it was to four `.at()`
(`currentLineDefFile`, `currentElementDefFile`,
`LineGenerator::getLineStandardsBody`, `isProportional`).
`ViewProviderViewPart`'s constructor asks for the standards body, so every
view of a part threw as its view provider was made.

**How the -1 got there:** the annotation preference page wrote it.
`changeEvent(LanguageChange)` runs `loadSettings()` again;
`loadLineStandardsChoices` empties the combo box, which emits "current index
-1"; `onLineStandardChanged`, connected since the first load, stored that and
then threw reading the definitions of standard -1, before the line that puts
the index back. The first exception in the reporter's log is that one: event
type 89 on `DlgPrefsTechDrawAnnotationImp`, 15:42:23. Upstream reads around
the same value ("likely caused by an old development version").

**Reproduced** in the dev tree with a copy of that `user.cfg`
(a load script kept in scratch, its outputs under `td\out-*`): 35 TechDraw views with no view provider, 154
TechDraw objects claimed by nothing in the tree, the abort above.

**Fix.**
- *The reads.* `lineStandard()` is never negative; the four readers take the
  first standard when the index names none and nothing when none was found;
  `getBodyFromString` gives no body for a name without a dot where it threw.
  `scaleType()`, `projectionAngle()`, `balloonArrow()` and `balloonShape()`
  read as their default when out of the table they index.
- *The writer.* The page refills the combo box with its signals blocked,
  connects the slot once, ignores "no current item" in the slot and in
  `saveSettings`; `setLineStandard` stores no negative index.
- *The restore* (`Gui/Document.cpp`). A view provider that throws while it is
  made leaves its object without one (`slotNewObject`), and one that throws in
  its update or its finish is reported and passed over (`drainDeferredRestore`).
  Either used to end the drain and drop every record still parked. Measured
  with the restore change alone and the bad preference still in: no abort, the
  35 views and the 10 pages reported one by one, everything else restored.

With all of it, on the same configuration: no exception, every object has its
view provider, every TechDraw object is claimed in the tree.
`tests/gui/techdraw-line-standard-out-of-range.py` (5 PASS; not scored against
the tree before the change, where the same script's first claim is the 35
missing view providers above). TestTechDrawApp 6 OK.

Not tried: the page drawn by the backend (`PageRendererVg`), which the 16:19
note found blank with the same exception in its paint. The default was
switched on and back off the same day (the reporter: "yes, make it default
on", then "change back the default renderer to qgraphicsview").

## 19. TechDraw: other indexes taken on trust (an audit asked) -- OPEN

**Asked (2026-10-06):** "audit for similar problem in techdraw". Read through
by a second agent, App and Gui, nothing run. What entry 18 already covers is
left out. In this fork an enumeration set to an index it does not have keeps
it, and `getValueAsString()` then throws; `getValue()` and `isValue()` do
not. So for enumerations the places are the `getValueAsString()` callers.

Runs at load, recompute or paint:
1. `Gui/ViewProviderProjGroupItem.cpp:64` -- `Type.getValueAsString()` in
   `updateData()`, for a `Type` a file can hold beyond its 10 entries.
2. `App/DrawViewDimension.cpp:1740, 1789, 1834` -- `SavedGeometry.getValues()
   .at(iReference)` where only "not empty" is checked; the 2D vertex variant
   (1767) has the check the other three lack. In `execute()`.
3. `Gui/QGIViewBalloon.cpp:440, 650` -- `BubbleShape.getValueAsString()`; a
   file value beyond 7 (the preference is now clamped).
4. `Gui/QGIViewDimension.cpp:772, 2112`, `Gui/QGIProjGroup.cpp:114` --
   `Type.getValueAsString()` of file enumerations, in page build and draw.
5. `App/DrawProjGroup.cpp:845, 921` -- `ProjectionTypeEnums[projConv]` and
   `[projConv + 1]`, a C array of three indexed with the `ProjectionAngle`
   preference (now clamped to 0..1). The two lines disagree by one: with the
   valid value 1 the second gives "Default" and throws.
6. `App/DrawProjGroupItem.cpp:172, 353` and `DrawProjGroup.cpp` (367, 418, 437,
   526, 554, 575, 955, 1260, 1330) -- the same `Type` as 1, on the App side.
7. `App/LandmarkDimension.cpp:126` -- `reprs.at(index)`, `ReferenceTags` from
   the file shorter than the 3D references.
8. `App/LineGenerator.cpp:253, 288, 351` -- a malformed row of a line
   definition file (`tokens.front()`, `begin()+2`, `.at(1)`), in the
   `LineGenerator` constructor.

Not a crash, wrong result:
- `LineGenerator.cpp:186, 192` compare the preference with `ANSI=0, ISO=1,
  ASME=2`, but the index is a place in the sorted file list, which with the
  shipped files is ANSI, ASME, ISO.
- The page writes `LineStyleHighlight`; `Preferences::HighlightLineStyle()`
  reads `LineStyleHighLight`. The setting is never read.
- `loadLineStyleBoxes` (`DlgPrefsTechDrawAnnotationImp.cpp`): `count() > style`
  is off by one, so the last style is never selected again and the next
  Apply stores 0.

Writers that can store -1: every TechDraw `Gui::PrefComboBox` saves
`currentIndex()`, which is -1 for an empty list -- `pcbLineGroup`, the four
line style boxes (their readers clamp), `pcbBalloonArrow` and `pcbArrow` after
a `setCurrentIndex` with a preference out of range. Task panels set document
properties straight from `currentIndex()` (TaskLeaderLine, TaskBalloon,
TaskDimension, TaskRichAnno); only `BubbleShape` has a reader that throws.

Dialog only: `TaskProjGroup.cpp:138-150`, `TaskSectionView.cpp:115`,
`TaskComplexSection.cpp:254`, `DlgPrefsTechDrawAnnotationImp.cpp:200-203`
(`lgNames.at(1..3)` on a line group row with fewer than four fields).

Not covered by the audit: the fixed `references.at(1)`/`.at(2)` of the
dimension helpers, the restore of cosmetics and centre lines, broken and
complex sections, details, templates, weld symbols, hatch and PAT parsing,
the command files and the Python.

## Inbox

Notes not sorted into an entry yet. Add a line here at any time, in any words;
it is read before each entry is started and moved up into the table.

- **2026-10-06 15:21, TechDraw section line.** "clicking a section line in
  techdraw page will trigger a section operation even without moving the
  section line. also the selection line position will be shifted each time the
  section is recomputed". Two symptoms: (a) a plain click on a section line,
  with no drag, starts the section operation -- a click is taken for a move;
  (b) the section line is drawn at a shifted position after each recompute of
  the section, so it drifts. ("selection line" in the second sentence read as
  the section line; the reporter's word kept.) Not said yet: which document
  and view, and which way or by how much it shifts.
- **2026-10-06 15:24, omni search (a change request).** "omni search first
  entry append a <space> after / to let user know to type a space." Wanted: in
  the first entry the omni search shows, a `/` is followed by a visible
  `<space>`, so that it is plain a space has to be typed after the slash.
  **Revised by the reporter, 15:33, and this is the one to do:** "Maybe we can
  make the space optional/implicit, so that if the word does not match any
  reserved keyword (param, cmd, etc.) treat it as object. We also accept space
  to disambiguate. how about that, in this way, no need to show <space>. also
  check wasm viewer, I remeber it already shows <space> there". So: after `/`,
  a word that is not a reserved keyword (`cmd`, `param`, ...) is an object
  query without any space; `/ ` with the space stays valid and is how to force
  an object query that would otherwise read as a keyword; nothing is shown for
  the space.
  Read from the source by the note-taker, nothing run: the grammar is
  `OmniSearch::parseInput` (`src/Gui/OmniSearch.cpp`) and its mirror
  `parseInput` in `src/Gui/Renderer/web/src/omni.tsx`, which says it follows
  the desktop's -- both change together. Today a `/` followed by anything but
  a full prefix (`/ `, `/cmd `, `/param `) is the chooser, so `/box` lists
  modes, not objects. The desktop's chooser rows are titled `/`, `/cmd`,
  `/param` (`OmniSearchEdit::setupChooser`). The browser's are titled `/ ` and
  `/cmd ` with a real trailing space in the string (`MODE_ROWS`) and no
  `/param` (not offered there on purpose); no literal `<space>` or other
  visible mark for it was found in `web/src` -- what it looks like on screen
  was not checked. Two cases the rule had to settle, **decided by the
  reporter, 15:36** ("yes, show both for partial keyword, keyword wins"):
  a partial keyword (`/c`, `/par`) lists both -- the matching mode rows and
  the objects matching the word; a full keyword (`/cmd`) is the keyword, and
  an object called `cmd` is reached with the space, `/ cmd`.
- **2026-10-06 15:28, omni search: the settings it collects (an audit asked).**
  "audit for all parameter/preference settings auto collected by omni search.
  ensure all settings has documentation, but not overly long. screen for those
  long ones that you may mistakenly added for development purposes". Asked
  for: (a) go through every parameter/preference setting the omni search
  collects automatically; (b) each must have documentation; (c) none of it
  overly long; (d) pick out the long ones in particular -- text an agent wrote
  as development notes that ended up as a setting's documentation.
- **2026-10-06 15:44, face highlight edge is jagged.** "face highlight
  silhouette shows jagged edge regardless whether msaa is used or not". The
  outline of a highlighted face is aliased, and switching MSAA on or off makes
  no difference to it. Not said yet: whether this is the hover highlight, the
  selection highlight or both, and which document.
- **2026-10-06 15:48, a long halt after enabling MSAA and pressing OK.**
  "while I am testing to toggle msaa, after first enabled it and click ok in
  preference page there is a long halt where the application is unresponsive.
  I know for some reason ok on preference page trigger updating all view
  provider. check if this is the cause of slow down." Asked for: find out
  whether the halt is the update of every view provider that OK on the
  preferences sets off, or the MSAA change itself. Not said yet: which
  document was open, and how long the halt was.
  Read from the source by the note-taker, nothing run or measured:
  - OK saves EVERY page, changed or not: `DlgPreferencesImp::applyChanges`
    calls `saveSettings()` on each page of each group
    (`src/Gui/DlgPreferencesImp.cpp`).
  - The update of every view provider exists and is one timer:
    `src/Mod/Part/Gui/PartParams.cpp`, `getTimer()`, 100 ms, then
    `ViewProviderPart::reload()` on every Part view provider of every
    document. It is started by (a) a tessellation preference whose VALUE
    changed -- the generated `update...` functions compare first, so a page
    saving the same number does not start it; (b) `RespectSystemDPI` or
    `ShapeInstancing` changing; (c) ANY notification of the `RenderCache` key
    in `Preferences/View` or the `Type` key in `Preferences/View/Render`
    (`InstancingGateObserver::OnChange` does not compare); (d) a renderer
    backend attaching or going away (`Render::Renderer::addActivityObserver`).
    So what to establish is whether OK re-notifies `RenderCache`/`Type` when
    they are written unchanged, and whether an MSAA change makes a backend
    detach and attach.
  - The MSAA change itself: `applyAntiAlias` in
    `src/Gui/PreferencePages/DlgSettings3DViewImp.cpp`, a delayed handler on
    the `AntiAliasing` key. A view with a renderer backend takes the new
    sample count in place (`View3DInventorViewer::applyRendererAntiAliasing`
    -> `setMSAASamples`); a view without one is CLONED and the original
    deleted, which rebuilds the whole view.
- **2026-10-06 15:51, every setting behind a generated helper class, so the
  omni search finds it (a change request, application-wide).** "audit the
  whole application and collect every Base::Parameter based settings into cog
  generated helper class access so that omni search can find it". And, added
  15:53: "in the process, also change the relevant code to monitor
  parameter/setting change and apply the change with delay handler". Asked
  for: (a) go through the whole application for settings read or written
  straight through the parameter system; (b) move each behind a cog-generated
  helper class, which is what registers a setting for the omni search;
  (c) while there, make the code that uses a setting watch it for changes and
  apply a change through a delayed handler, instead of reading it once or
  needing a restart or a preferences OK.
  Goes with the 15:28 note above (documentation of the settings the omni
  search collects): a setting moved here needs its short documentation too.
  Read from the source by the note-taker, for the size of it, nothing changed:
  the omni search lists settings out of `App::ParamRegistry`
  (`src/Gui/OmniSearch.cpp`), which the classes generated by
  `src/Tools/params_utils.py` fill -- 14 of them today (App: Document, Group,
  Link; Gui: Expr, OpenView, Overlay, Render, ReportView, Tree, View; Mesh;
  Part App and Part Gui; Spreadsheet). Against that, 377 source files outside
  `3rdParty` call `GetParameterGroupByPath` or `ParamGet` directly. The delayed
  handler asked for in (c) has a precedent in `ParamHandlers::addDelayedHandler`
  (used by `DlgSettings3DViewImp::attachObserver`) and in the generated
  classes' own `on...Changed` hooks.
- **2026-10-06 15:56, TechDraw: errors without end after switching the page's
  renderer.** "when techdraw page is opened with qgraphics rendering, and then
  switching to bgfx vg renderer, there is continuous error output". A page
  opened while drawn by QGraphics, then switched to the bgfx vector renderer:
  the report view fills with errors and does not stop. The text, from the
  reporter (16:01): "QBackingStore::endPaint() called with active painter; did
  you forget to destroy it or call QPainter::end() on it?". Not said yet: how
  the switch was made (preference, menu, command), which document, and whether
  a page opened directly with the bgfx vector renderer is clean.
  Read from that session's report log by the note-taker (a copy is
  `..\dl\handson\2026-10-06\techdraw-switch-report-view.log`; nothing run):
  - The Qt warning is the tail, not the cause. From 15:58:40 on, the page's
    viewport throws inside its paint event, 82 times in two minutes: "CAUGHT
    ... std::exception: invalid vector subscript", "Unhandled std::exception
    caught in GUIApplication::notify", event type 12 (Paint), receiver the
    `QWidget` under `TechDrawGui::QGVPage (PageView)` in
    `TechDrawGui::MDIViewPage (Page)`. Each throw leaves the paint with its
    painter still open, which is what Qt then complains of -- 630 times, with
    "QPainter::begin: A paint device can only be painted by one painter at a
    time", "QPixmap::fill: Cannot fill while pixmap is being painted on" and
    "QPaintDevice: Cannot destroy paint device that is being painted" beside
    it. So the thing to find is the vector indexed out of range in the page's
    paint after the switch.
  - The same exception text appears earlier in that log from somewhere else,
    twice, each time the preferences dialog was opened (15:43:32, 15:52:46):
    "C++ exception thrown for 'TechDrawGui::DlgPrefsTechDrawAnnotationImp'
    (invalid vector subscript)". Possibly a problem of its own -- the TechDraw
    Annotation preferences page failing to load -- and possibly the same
    out-of-range read.
- **2026-10-06 16:05, the view cell menu opens a spreadsheet nobody asked
  for.** "where there is a spreadsheet opened, I click 'View cell menu' of the
  spreadsheet view and change it to a 3d view. then I click 3d view again
  without any selection, a spreadsheet view is auto created. If I have one 3d
  view and one techdraw page, and I click 'view cell menu' of the techdraw
  page, it auto switch to spreadsheet for that view." Two cases:
  (a) a spreadsheet view open; its cell changed to a 3D view through the view
  cell menu; then "3D view" chosen again with nothing selected -- a spreadsheet
  view is created by itself;
  (b) one 3D view and one TechDraw page open; opening the view cell menu of
  the TechDraw page alone -- no choice made in it -- turns that cell into a
  spreadsheet.
  Common to both: the menu, or its 3D-view entry, falls through to "spreadsheet"
  when it has nothing to act on. Not said yet: which document, and whether the
  document has more than one spreadsheet.
- **2026-10-06 16:08, the view cell menu of a TechDraw page lists every
  TechDraw object.** "if techdraw page is on one view, and I click 'view cell
  menu' I can see all techdraw objects listed in the menu, like 'Page',
  'Detail', 'Dimension', etc." The menu that should offer what a cell can show
  lists the page's own child objects -- detail views, dimensions and the like
  -- next to the page, as if each could be shown in a cell. Same menu as the
  16:05 note above; possibly the same list being built too widely.
- **2026-10-06 16:09, the view cell menu changes the wrong cell.** "with two
  3d view and one spreadsheet view side by side. I click 'view cell button' on
  one of the 3d view and select a techdraw page, the spreadsheet view switched
  to techdraw." Three cells side by side, two 3D views and a spreadsheet; the
  view cell button of one of the 3D views is used to choose a TechDraw page;
  the SPREADSHEET cell becomes the TechDraw page, not the 3D view whose button
  was pressed. Third note on this menu (16:05, 16:08): here the choice is
  right and the cell it lands in is wrong. Not said yet: which of the two 3D
  views, and which cell was the active one at the time.
- **2026-10-06 16:19, TechDraw drawn by bgfx is broken outright, not only
  after a switch** (corrects the 15:56 note above). "it seems bgfx rendering
  of techdraw is broken right now. I restarted the app with the rendering
  option turned on. and it still gives me blank page and continuous error
  'QBackingStore::endPaint() called with active painter; did you forget to
  destroy it or call QPainter::end() on it?'" So: restarted with the option
  already on, page opened fresh -- blank page, the same stream of errors. The
  switch in the 15:56 note is not needed to get it.
  Read from the restarted session's report log by the note-taker (copy:
  `..\dl\handson\2026-10-06\techdraw-bgfx-after-restart-report-view.log`;
  nothing run):
  - Same cause as before under the Qt warning: "invalid vector subscript"
    thrown in the page viewport's paint, again and again from 16:16 on.
  - WORSE, and new: the same exception also cuts the LOAD short. At 16:16:16,
    opening `scanner.FCStd`: "<Gui> Document.cpp(3700): restore scanner:
    deferred view provider restore aborted (invalid vector subscript), 320
    objects fall back to defaults". The view properties of 320 objects were
    not restored from the file in that session -- colours, visibility, display
    modes come up as defaults. Saving the document from that session would
    write those defaults over what the file had.
  - The FIRST of the 215 caught exceptions of that session is not a paint:
    16:17:17, "event type 2" (a mouse press), "receiver QWidget
    'ViewAreaMenuButton'" under `Gui::ViewAreaCell`. Pressing the view cell
    menu button threw the same "invalid vector subscript". That ties this to
    the three view cell menu notes above (16:05, 16:08, 16:09): what that menu
    then did may be what is left of an operation an exception cut short.
  - Just before it in the log, for context: "DVS: SectionOrigin doesn't
    intersect part in SectionView003", "DVS::prepareShape - failed to build
    shape SectionView003 - Bnd_Box is void". Whether the section view that
    fails to build is what the out-of-range read trips over is not known.
