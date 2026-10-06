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
| 5 | 2026-10-06 | title bar with the workbench bar docked: the menu does not unfold on hover | OPEN |
| 6 | 2026-10-06 | maximized with the custom title bar: sometimes no margin at the top | OPEN |
| 7 | 2026-10-06 | crash after answering Yes to the recompute question on `scanner.FCStd` | STAGED, cause of the GL error open |
| 8 | 2026-10-06 | `scanner.FCStd`: the migration recompute fails | OPEN |

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

## 5. Title bar with the workbench bar docked: the menu does not unfold -- OPEN

**Reported (2026-10-06):** "in the customized titlebar is docked with
workbench sometimes malfunction, the menu bar will not show when mouser hover.
I can fix it by re-docking the workbench bar." Then: "the customized title bar
problem seem to happen before, check git history."

**Seen in the running copy (state at the time, not known to be the failing
one):** `TitleBarWidget` 1920 x 35 at the top of the main window;
`FoldableMenuBar` 54 x 35 holding a `QMenuBar` of 558 x 21; the `Workbench`
toolbar (851 x 35, `WorkbenchTabWidget` 832 x 29) parented to
`MenuBarLeftArea`, not to the main window.

**History to read:** `b39dd73fed` fold the title bar menu behind the logo,
`c35c0b05a6` a hamburger, a hover, and a switch, `1863cf74a5` let the title bar
take the workbench toolbar, `929082dc3c` ignore the reflex click that folds a
just-unfolded menu, `d6183b77a1` and `27c5ba835f` (keyboard), `d64805c1a1` a
menuBar() call must not shove the toolbars under the title bar.

## 6. Maximized with the custom title bar: sometimes no margin at the top -- OPEN

**Reported (2026-10-06):** "when maximized, sometimes, with the customized
toolbar, it leaves no margin at top. sometimes it is fine."

**Seen in the running copy (maximized, looking right at the time):** window
geometry (0, 0, 1920 x 1040), frame (-8, -8, 1936 x 1056), screen 1920 x 1080
at 100%, title bar at y = 0.

**History to read:** `8a7f412fe3` stop a maximized custom title bar hanging
off the top of the screen, `d712eae660` fix the 8px input offset of a
maximized custom title bar.

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

## 8. `scanner.FCStd`: the migration recompute fails -- OPEN

**Seen (2026-10-06), twice:** the file is from FreeCAD-Link 2025.1020 and asks
for a recompute "for migration purpose"; the recompute ends in "Recompute
failed!". In the report view: `SubShapeBinder.cpp(477): scanner#Binder018
failed to obtain shape from scanner#Binder008.?Face1` (`Null shape`), "auto
change element reference" on `Helix001.Profile` and `Pocket037.Profile`, and a
run of TechDraw "no exact match for changed 2d reference". Not looked at yet.

## Inbox

Notes not sorted into an entry yet. Add a line here at any time, in any words;
it is read before each entry is started and moved up into the table.

- **2026-10-06 11:58, 3D view lags behind the mouse.** "the 3d view seems
  lagging in response to mouse movement and wheel. a mouse over highlight is
  visibly delayed a few hundries of ms. and moving away the mouse to an empty
  area does not cancel the previous highlight. mouse wheel zoom is also visibly
  lagging. after stopping wheeling, I can see the 3d view is catching up with
  the lingering zooming operation." Three symptoms in it: (a) preselection
  highlight arrives a few hundred ms late; (b) moving to empty space does not
  clear the highlight; (c) wheel zoom queues up and keeps playing after the
  wheel has stopped. Added 12:01: "scanner.FCStd was open when it lagged" (the
  677-object file of entries 2, 7 and 8). Not said yet: whether it is the same
  with an empty or small document.
- **2026-10-06 11:58, dark theme colors.** "checkbox border and customized
  toolbar maximize/minimize icon got bad color in dark theme. audit for other
  similar UI color problem." Two named: the checkbox border, and the
  maximize/minimize icons of the custom title bar. Asked for beyond those: an
  audit of the dark theme for other widgets with the same kind of wrong color.
- **2026-10-06 11:58, TechDraw.** "techdraw dimension/cosmetics is covered by
  face filling." Dimensions and cosmetic elements are drawn under the face
  fill instead of over it.
- **2026-10-06 11:58, grouped messages in the console (a change request).**
  "do not use underscore in console grouped message, intead, put a clickable
  expansion icon before the message (note, those grouped message should still
  align with other normal message, put the icon in front of it, in the margin
  area)." Wanted: no underscore on a grouped message; a clickable expand icon
  ahead of it; the message text itself stays aligned with ordinary messages,
  the icon sitting in the margin.
