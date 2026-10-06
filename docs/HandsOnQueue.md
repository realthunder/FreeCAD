# Hands-on queue

Problems found by running the program by hand, in the order they were
reported, worked in turn. One entry per problem: what was seen (the reporter's
words where there are any), what it turned out to be, and the commit that
closed it. The copy under test is the staged one -- `docs/DevEnvironment.md`,
"A second env for hands-on testing" -- so a fix reaches the reporter only at
the next stage, and each entry says which stage has it.

Stages so far: 2026-10-06 07:56 (`84c14e12d5`, the first), 2026-10-06 11:23
(`c1028260e3`: entries 1, 2, 4, 7).

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
| 3 | 2026-10-06 | tooltips are clipped: navigation style, and toolbar buttons with an icon | FIXED |
| 4 | 2026-10-06 | status bar dimension reads `100 mm x 80 mm`, wanted `100 x 80 mm` | STAGED |
| 5 | 2026-10-06 | title bar with the workbench bar docked: the menu does not unfold on hover | FIXED if it is entry 6's cause; to confirm |
| 6 | 2026-10-06 | maximized with the custom title bar: sometimes no margin at the top | FIXED |
| 7 | 2026-10-06 | crash after answering Yes to the recompute question on `scanner.FCStd` | STAGED, cause of the GL error open |
| 8 | 2026-10-06 | `scanner.FCStd`: the migration recompute fails | FOUND in full; the helix FIXED; the rest is entries 14 to 16 |
| 9 | 2026-10-06 | the 3D view lags behind the mouse: hover highlight, wheel zoom | OPEN |
| 10 | 2026-10-06 | a 3D view is slow to take a new size | OPEN |
| 11 | 2026-10-06 | dark theme: wrong colors (checkbox border, title bar buttons), audit asked | OPEN |
| 12 | 2026-10-06 | TechDraw: dimensions and cosmetics are covered by the face fill | OPEN |
| 13 | 2026-10-06 | report view: grouped messages with an expand icon in the margin, no underscore (change request) | OPEN |
| 14 | 2026-10-06 | a Draft with no neutral plane given turns the other way after a recompute (from entry 8) | FOUND |
| 15 | 2026-10-06 | a Pad "up to first" gives a third result (from entry 8) | OPEN |
| 16 | 2026-10-06 | faces of a "Mutated" copy-on-change binder are renamed by every recompute in a new session (from entry 8; the old build too) | FOUND |

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

## 3. Tooltips are clipped -- FIXED

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

## 6. Maximized with the custom title bar: sometimes no margin at the top -- FIXED

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

## 14. A Draft with no neutral plane given turns the other way -- FOUND

**From entry 8.** `Draft` in `scanner.FCStd`: face `Face6` of `Pad036`, 11 deg,
`Reversed` on, no neutral plane and no pull direction. Old build: valid, 285.76.
This build: "Failed to create draft:", and with `Reversed` OFF the same 285.76.

**Not the kernel's draft.** On a plain box every case agrees between 7.7.2 and
8.0.1, guessed plane included (`entry8-box-*.txt`), and the pad's shape as
stored in the file, drafted on its own in this build, gives the old result.

**Cause:** with no neutral plane given, `Draft::execute` guesses one from "the
first edge of the first face". Recomputed here, `Pad036`'s top face lists its
four edges in another order than the stored shape has them -- the first is the
opposite edge -- so the guessed plane is on the other side, the pull direction
points the other way, and `Reversed` means the opposite
(`entry8-draft*.txt`). Whether the order comes from OCCT 8.0.1's prism or from
the ported Pad was not established.

**Not fixed.** The guess depends on an order nothing promises. A file saved
with a guessed plane has no record of which edge it was; one way out is to
record the edge by its mapped name the first time, taken from the stored shape
on restore. To decide with the reporter.

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

## 16. Faces of a "Mutated" binder are renamed by every recompute in a new session -- FOUND

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

**Not fixed.** The names would be stable if the copies' ids were replaced by
those of the objects they are copies of. Files saved before that would break
once more, unless a missing name is also looked up with the copies' ids taken
out of both sides. To decide with the reporter.

## Inbox

Notes not sorted into an entry yet. Add a line here at any time, in any words;
it is read before each entry is started and moved up into the table.

