# Hands-on queue

Problems found by running the program by hand, in the order they were
reported, worked in turn. One entry per problem: what was seen (the reporter's
words where there are any), what it turned out to be, and the commit that
closed it. The copy under test is the staged one -- `docs/DevEnvironment.md`,
"A second env for hands-on testing" -- so a fix reaches the reporter only at
the next stage, and each entry says which stage has it.

Stages so far: 2026-10-06 07:56 (`84c14e12d5`, the first), 2026-10-06 11:23
(`c1028260e3`: entries 1, 2, 4, 7), 2026-10-06 13:59 (`489c64799c`: entries 3, 5, 6, and
the helix of entry 8), 2026-10-06 14:44 (`6b1bd3f434`: entry 14), 2026-10-06 17:44
(`f7d3aa0cf2`: entries 16 and 18), 2026-10-07 10:37 (`7e94bff8d0`: entries 9 to 13, 20
and 21), 2026-10-07 14:23 (`1c8781a7e1`, the code of `c7a27b5a85`: entries 15, 17, 19,
22, 23 and 27; the build session's smoke test on the staged copy, 41 of 41 GUI
checks).

**Two documents since 2026-10-07 11:15, one writer each** (asked for by the
reporter, agreed between the two sessions). This one is the REQUEST side and
the note-taking session alone writes it: the table, what was reported in the
reporter's words, what they added or decided later, and the Inbox. The WORK
side -- per entry number its state, cause, fix commit, tests, measurements and
the stage that has it -- is `docs/HandsOnLog.md`, written by the build and
test session alone. The findings written inside entries 1 to 28 are as they
stood at that time and are not added to here; the log has what came after,
and each new stage. The State column IS kept up: when the build session
finishes something it asks the note-taker to set the state, in one line, and
the line points at the log for the rest (the reporter's rule to both
sessions, 2026-10-07; entry 15 was the first).

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
| 9 | 2026-10-06 | the 3D view lags behind the mouse: hover highlight, wheel zoom | STAGED |
| 10 | 2026-10-06 | a 3D view is slow to take a new size | STAGED with entry 9 |
| 11 | 2026-10-06 | dark theme: wrong colors (checkbox border, title bar buttons), audit asked | STAGED: the two named and four the audit found |
| 12 | 2026-10-06 | TechDraw: dimensions and cosmetics are covered by the face fill | STAGED (they were transparent, not covered) |
| 13 | 2026-10-06 | report view: grouped messages with an expand icon in the margin, no underscore (change request) | STAGED |
| 14 | 2026-10-06 | a Draft with no neutral plane given turns the other way after a recompute (from entry 8) | STAGED |
| 15 | 2026-10-06 | a Pad "up to first" gives a third result (from entry 8) | STAGED 2026-10-07 14:23, fixed `1047cc0647`: not the pad -- a refine in the feature on top (Helix002) wrote into the pocket's shape. What was left is no defect: Pocket040 comes out at radius 12 for the file's 13 because its negative Fit grew in the old build; DECIDED by the reporter 2026-10-07 13:20: set `Pocket040.Fit` to +0.5 in the file (`docs/HandsOnLog.md`) |
| 16 | 2026-10-06 | faces of a "Mutated" copy-on-change binder are renamed by every recompute in a new session (from entry 8; the old build too) | STAGED |
| 17 | 2026-10-06 | `Sketch043`, `Sketch055`: "Missing external geometry reference", seen once the binders of entry 16 are valid | STAGED 2026-10-07 14:23, fixed `3c8cd63032`: the sketches' references into Binder017 (a binder of the moved Binder008) are found again; Pad033 then loses its profile because Sketch043 really changes -- a question for the reporter (`docs/HandsOnLog.md`). DECIDED 2026-10-07 14:34: the binder moving with the group is as designed (the file's sketch is stale); change request, on hold with the entry: the import and the binder command record the Context they know; and a defect found by running it: a recorded context whose path is gone is neither used nor replaced |
| 18 | 2026-10-06 | TechDraw pages do not load: "invalid vector subscript", the views loose in the tree, 320 objects restored to defaults | STAGED |
| 19 | 2026-10-06 | TechDraw: other indexes taken on trust (an audit asked) | STAGED 2026-10-07 14:23, fixed `805b5afb25`: out-of-range enumerations repaired at restore, the list indexes checked, the projection angle off by one, the three wrong results (line standard compare, highlight key, last line style) and combo boxes no longer storing -1; what was left alone is listed in `docs/HandsOnLog.md` |
| 20 | 2026-10-06 | TechDraw: crash when the page is switched to the backend's renderer; and what it then drew | STAGED, the double draw too |
| 21 | 2026-10-06 | TechDraw: a click on a section line starts a section, and the line shifts at each recompute | STAGED |
| 22 | 2026-10-06 | omni search: `/word` with no space is an object query; `/ word` forces it (change request, decided) | STAGED 2026-10-07 14:23, fixed `5aedd5cf83`: "/word" is an object query, "/ word" forces it, a keyword in full is the keyword, the beginning of one lists modes and objects together; the browser viewer's grammar follows (its bundle not rebuilt) |
| 23 | 2026-10-06 | omni search: every setting it collects has documentation, none of it long (an audit asked) | STAGED 2026-10-07 14:23, fixed `c7a27b5a85` (and `08b8f009aa`): 574 settings audited, 221 had no documentation and 94 ran past 400 characters; all have a short text now and a test keeps it so. Side findings for the reporter in `docs/HandsOnLog.md`. The defaults FIXED `02cab053df`, not staged: OK on a fresh profile changed 23 settings and stored 2 under a wrong type, 14 of them a generated page's spin box clamping its default to 99; a test keeps it so |
| 24 | 2026-10-06 | every `Base::Parameter` setting behind a cog helper class so the omni search finds it, applied through delayed handlers (change request, application-wide) | OPEN |
| 25 | 2026-10-06 | the outline of a highlighted face is jagged, MSAA on or off | OPEN |
| 26 | 2026-10-06 | a long halt after enabling MSAA and pressing OK in the preferences | FIXED `175ffce199`, not staged: the FIRST OK of a profile held the program 11 to 15 s (780 keys stored for the first time and taken for changes: stylesheet set again 4.2 s, every Part view provider re-meshed 3.2 s, language activated again about 2 s); 0.9 s now (`docs/HandsOnLog.md`) |
| 27 | 2026-10-06 | the view cell menu: opens a spreadsheet nobody asked for, lists every TechDraw object, changes the wrong cell | STAGED 2026-10-07 14:23, fixed `fa2ada985c`: the menu made a spreadsheet view by asking for it, listed a page's views, and placed a pick by the general policy instead of into its cell; all three gone, spreadsheets now listed by type like pages (`docs/HandsOnLog.md`) |
| 28 | 2026-10-06 | `scanner.FCStd` restores with a wrong colour, sometimes (the motor body light blue for light grey) | OPEN |
| 29 | 2026-10-07 | view cells: transparent frames that show a split, a join and a resize while dragged (every cell the drag changes); corner handles on an opaque background, the cell menu button too when hovered; a thinner border between cells; a minimum cell size setting, default 200 (change request, decided) | OPEN |
| 30 | 2026-10-07 | the dark and light overlay stylesheets integrated into the Dark and Light preference packs (a task asked; what "integrated" covers to confirm); and the long freeze when an overlay stylesheet is applied, to investigate; the Python console's background in both packs, so a theme can take an overlay preset's away again | OPEN |
| 31 | 2026-10-07 | report view: "Go to end" on by default (change request) | FIXED `47b5e72c79`, not staged: "Go to end" is on for a profile that never stored it (`docs/HandsOnLog.md`) |
| 32 | 2026-10-07 | some sub menus are transparent with blue text (Tools > Command history): find out why; transparent menus off by default | FIXED `b960092ea5`, not staged: the see-through menus are single menu objects shared between a pop-up over the 3D view and an entry of the main menu, and a themed session with no menu sheet chosen took the see-through sheet; now no sheet chosen = an ordinary menu, the see-through ones a choice in Preferences > Theme. A question for the reporter (`docs/HandsOnLog.md`) |
| 33 | 2026-10-07 | a cmd window pops up briefly at the first document opened after start | FIXED `ef4df215b5` (the cycles submodule at its `35a3bd898`), not staged: the CUDA probe ran `cmd.exe /c where nvcc` through `popen` at the first 3D view; it searches the PATH without a shell now, and the session starts no process at all. Neither commit pushed; the cycles one has to go first (`docs/HandsOnLog.md`) |
| 34 | 2026-10-07 | TechDraw's preselection colour sometimes does not follow the theme (stays yellow after classic, or is blue) | FIXED `3d7b4c30fd`, not staged, as decided: Dark and Light store TechDraw's `PreSelectColor`, the blue of the 3D view's highlight; a test switches Classic, Dark, Light, Classic (`docs/HandsOnLog.md`) |
| 35 | 2026-10-07 | TechDraw (`scanner.FCStd`, Page003): now and then a click starts a recompute; a dimension (Dimension134) cannot be selected; selecting it in the tree can recompute and clear the selection. Asked: an audit of TechDraw for unnecessary recomputes | OPEN |
| 36 | 2026-10-07 | TechDraw drawn by the backend: dashed lines do not behave as Qt's do (view frame, section line, hidden line, and so on), zoom above all | OPEN |
| 37 | 2026-10-07 | TechDraw: the edge style "Chain" is not drawn dashed, by either renderer, though the style combo box shows it dashed | OPEN |
| 38 | 2026-10-07 | omni search: an obvious freeze the first time it is brought up | FIXED `bb31f8820b`, not staged: the first bring-up loaded and rendered the icon of every command (609) before showing the box, 0.99 s + 0.28 s on the reporter's configuration with `scanner.FCStd` open; 0.15 s + 0.07 s now (`docs/HandsOnLog.md`) |
| 39 | 2026-10-07 | MSAA has not reached any view since 2026-09-07 (found by the build session on entry 26) | FIXED `c7d115e576`, not staged: with "MSAA 4x" chosen the backend could not create its scene targets and drew without multisampling from then on, on every backend; the depth is write-only under MSAA now. The reporter's case on the fixed tree: 0.75 s in all, both views at 4 samples (`docs/HandsOnLog.md`) |

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

## 9. The 3D view lags behind the mouse -- STAGED

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

**Found (2026-10-07), measured.** On Windows the backend runs on Direct3D 11 and reaches the Qt
view through a read-back (docs/DeviceAdoption.md section 10). That route is
pipelined: a frame queues a copy of itself, the copy lands about two frames
later, and a frame that finds nothing landed shows the previous picture
again (`BGFXView::blitReadback`). Nothing asks for the frame that would
show the copy. So when the redraws stop, the view is left one or two
redraws behind, for as long as nothing else redraws it. A TechDraw page is
not: its layer is read with a wait.

The probe reads what the view's GL widget holds for the screen, with no
paint of its own (`..\dl\handson\2026-10-07\entry9-lag10.py`, a box, the
mouse moved by events), twice each way, the same both times:

| | the mouse rests on a face, a second later | it leaves, a second later |
|---|---|---|
| as it is | no highlight; it comes with ONE more redraw | the highlight is still there; gone after TWO more redraws |
| `FC_BGFX_READBACK_SYNC=1` (every frame waits for its copy) | highlighted | gone |

That is (a) and (b) as reported. (c), the wheel, was not measured; it is
what the same two frames behind would look like under a stream of wheel
steps. `entry9-highlight-held-pipelined.png` shows the four states.

What the wait costs, from the renderer's own report (`DebugTiming`, "render
readback composite"), a box and then `scanner.FCStd` turned through 150
frames: `wait` 0.28 to 0.43 ms a frame, the composite 0.60 to 0.86 ms in
all against 0.36 to 0.76 pipelined. A frame took 10.0 ms either way in that
loop (something else sets that floor).

**Two ways to fix it, as they were put to the reporter.**
1. Wait for the copy on every frame (what the switch does, made the
   default; the switch kept to turn it off for benchmarks). No lag at all,
   about a third of a millisecond a frame here. On a scene where the GPU,
   not the CPU, is the limit it gives up what pipelining bought -- the
   document that measured the route had 86 ms frames and paid 2.2 ms for
   the composite; what the wait adds there is not measured.
2. Stay pipelined, and have the host ask for one waiting frame when the
   redraws stop (a short timer). Keeps the throughput; leaves the two
   frames of lag while moving, and the highlight a timer's length late.

My recommendation is 1: the lag is what the reporter sees on every
document, and the cost is small where it was measured.

**Decided (the reporter, 2026-10-07):** "Make the frame mode a setting.
Default to wait. Turn it on for animation."

**Fix.** `Render/ReadbackFrameMode`, a generated setting (the omni search
lists it), read before every frame, so a change shows at the next one:

| value | a frame waits for its copy |
|---|---|
| `Wait` | always |
| `Pipelined while animating` (the default) | unless the view is redrawing by itself: a camera animation, a spin, the backend's animated content |
| `Pipelined` | never; one waiting frame follows when the redraws stop |

"Turn it on for animation" is read as the second row: in a run of frames
the next one shows this one anyway, and the wait buys nothing there.
Confirmed by the reporter, 2026-10-07: "default is right".

How it is built (docs/DeviceAdoption.md section 10 has it in full): the
host says before each frame whether it may be pipelined
(`Renderer::setFramePipelined`), and after a pipelined frame it owes the
backend one frame that waits (`Renderer::frameTrails`). The 3D view does
it through a helper, `Gui::ReadbackFramePacer`
(`View3DInventorViewer::renderScene`). The owed frame is asked for by a 50 ms
timer that every further frame puts off, so it comes once, when the run
has stopped -- which is what makes `Pipelined` a mode that can be chosen,
and what brings the screen up to date after an animation's last frame. A
capture waits as before. `FC_BGFX_READBACK_SYNC` still holds one form for
a benchmark leg, whatever the setting: 1 every frame waits, 0 none does
and nothing settles (`scripts/composite-cost.sh` sets it for both legs
now).

The other host, the shared canvas of a split view (`View/UnifiedCanvas`,
off by default), is left as it was, and the setting does not reach it: it
draws each cell as a capture, which has always waited. A probe of it --
two cells, three resizes, each mode -- holds the settled picture a second
later on this build and on the copy staged 17:44 alike. Its cells are
therefore never pipelined, in an animation either; making them so is a
change to a path that has only ever run serialized, and it was not asked
for.

**Scored.** `tests/gui/readback-frame-mode.py`, 25 claims: in each of the
three modes the face the mouse rests on is highlighted a second later and
plain again a second after the mouse has left, the view holds its settled
picture a second after eight wheel steps, after each of three resizes and
after a spin is stopped; and, from the renderer's own report, no frame of
a spin waits in the two pipelined modes and every one does in `Wait`.
25 PASS; on the copy staged 17:44, 22 FAIL (everything but the two "no
frame of a spin waits" and the default's name). The wheel is symptom (c),
which had not been measured: 365 to 852 samples of the view differ a
second after the last step there, 0 here.

The two probes that found it, at the default
(`..\dl\handson\2026-10-07\`): the highlight samples over plain read
402 a second after the mouse came to rest and 0 a second after it left,
both rounds; the three resizes differ in 0 samples.

**What the wait costs**, measured by switching the setting in one process,
legs alternated and the first discarded (`entry9-modecost.py`,
`entry9-gpubound.py`; swap interval 0, Direct3D 11). The camera is turned
a degree and the view redrawn, which is not an animation, so `Wait` waits
on every frame and `Pipelined` on none:

| scene | `Wait` | `Pipelined` | the wait costs |
|---|---|---|---|
| a box | 3.45 ms | 2.90 ms | +0.56 ms |
| `scanner.FCStd` (47 to 68 draws in view) | 2.70 | 2.51 | +0.20 |
| 300 transparent planes face-on, 1280 x 638 (GPU 5 to 6 ms a frame) | 10.10 | 9.57 | +0.53, 5.5% |
| the same, run again | 9.20 | 8.89 | +0.31, 3.5% |
| 1200 of them (GPU 5 to 7 ms) | 14.02 | 13.31 | +0.71, 5.3% |

And what the default gives back in an animation, the view's own spin, in
frames a second: 47.0 against 46.3 with `Wait` on the 300 planes (+1.4%;
run again, 48.4 against 47.4, +2.1%), 39.8 against 38.8 on the 1200
(+2.6%).

The renderer's own account of the wait is 0.2 to 0.5 ms a frame in all of
them, and it does not grow with what the GPU has to do. Read from bgfx,
that is how Direct3D 11 has to behave: the copy is read with a blocking
map inside the frame boundary after the one that queued it
(`RendererContextD3D11::readTexture`), pipelined or not, so the GPU is
caught up with once a frame either way and waiting adds only the two
nearly empty frame boundaries bgfx wants before it hands the buffer over.
The worry of option 1 -- a scene the GPU limits -- does not arise on this
backend. It was not measured on Direct3D 12, Vulkan or Metal, and no scene
here had the GPU as its limit (5 to 7 ms of GPU in a 10 to 14 ms frame was
the most the planes gave).

**Found on the way: the timing report's `gpu` figure was garbage once
frames waited** (`render frame: ... gpu 84274584.65ms`). bgfx's Direct3D 11
timer took a frame's two timestamps as soon as the END one had a result and
then read the frequency from the disjoint query without looking at whether
THAT had one -- it is ended last, and when it had none the frequency
published was whatever the stack held (1, or 2250096623200, for the real
1000000000; a probe printed them). A frame that waits reads the timer
right behind the frame, which makes the race common. Fixed in the fork's
bgfx (`TimerQueryD3D11::update` asks the disjoint query first and leaves
the last result standing when a query has nothing to say).

## 10. A 3D view is slow to take a new size -- STAGED

**Reported (2026-10-06 12:04):** "when I create a new document, the mdi window
will zoom to fit. the background gradient is stuck at its old size and visibly
delayed almost a second and more to fit the window. same for switching the
view window, the background together with content stuck for too long to fit.
these may or may not be related to the mouse problem".

Two cases: (a) a new document's view growing to fill the MDI area -- the
background stays at the old size for a second or more; (b) switching between
view windows -- background and content both stay at the old size too long.
Possibly one delay behind this and entry 9, in the reporter's reading.

**Found (2026-10-07): it is entry 9's cause, and its fix is this one's.**
The probe (`..\dl\handson\2026-10-07\entry10-lag11.py`) gives the main
window a new size three times and compares what the view holds a second
later with what it holds after four more redraws:

| | samples that differ a second after the resize |
|---|---|
| as it is | 67181 of 129360, 18339 of 40560, 49545 of 85410 |
| `FC_BGFX_READBACK_SYNC=1` | 0, 0, 0 |

**Fixed with entry 9** (`Render/ReadbackFrameMode`): a view that takes a
new size is not animating, so its frame waits for its copy at the default,
and in `Pipelined` the frame that waits follows 50 ms later. The three
resizes of the probe differ in 0 samples in every mode
(`tests/gui/readback-frame-mode.py`).

`entry10-a-second-after-resize-pipelined.png`: the old picture, at its old
size, in a corner of the view that has grown. The frame at the new size is
drawn and its copy queued; what is shown is the last copy that landed, the
old one; and the staging buffers cannot be rebuilt for the new size while a
copy is in flight (`BGFXView::ensureReadbackTarget`), which holds it one
frame longer still. With every frame waiting for its copy none is ever in
flight when the next begins.

## 11. Dark theme: wrong colors -- STAGED

**Reported (2026-10-06 11:58):** "checkbox border and customized toolbar
maximize/minimize icon got bad color in dark theme. audit for other similar UI
color problem."

Two named: the checkbox border, and the maximize/minimize icons of the custom
title bar. Asked for beyond those: an audit of the dark theme for other
widgets with the same kind of wrong color.

**Seen** in the dev tree, a fresh configuration, the Dark theme applied
(`Gui.applyTheme("Dark")`: the parameterized sheet, `FreeCAD.qss` with
`parameters/Dark.yaml`) and the custom title bar on; photographs of the
title bar, of a sampler of standard widgets in every state and of the
program's own surfaces are in `..\dl\handson\2026-10-07\entry11-*`.

**The two that were named.**
- *The title bar's buttons.* The kit ships each glyph twice, a dark stroke
  and a light one (`window-minimize.svg`, `window-minimize-dark.svg`), and
  `WindowDecorationButton` loaded the dark stroke whatever the theme: the
  three buttons were there and all but invisible, 1 to 2 of 255 lighter
  than the bar. The button takes the glyph that reads the way its text
  does now -- the light stroke where the palette's text is light, which is
  the style sheet's `color` once the widget is polished -- and takes it
  again when the palette changes. In the kit, marked LOCAL DIVERGENCE.
- *The check box.* `CheckBoxBorderColor` was `@GeneralBorderColor`, which
  in the Dark set is black: a box in the field colour with a black edge in
  a dialog one step lighter shows no edge, and an unchecked box read as a
  darker patch. Upstream has the same line; it has since lightened its
  whole dark base (`c365ff6338`, 2026-09-20, `PrimaryColor` #191919 to
  #323232), which is a change of the theme's look and was not taken here.
  The edge is `@PrimaryColorLighten5` now (#646464), and the radio
  button's follows it; the same parameter draws the indicator of a
  checkable group box and of a tree, list or table item.

**The audit**, by photograph: the sampler (check boxes, radio buttons,
group boxes, item views, combo and spin boxes, line edits, buttons, tool
buttons, slider, progress and scroll bars, tabs, a menu), the preferences
dialog (General, 3D View, Colors, Theme), the tree, the property editor
(both tabs), the report view, the Python console, a task panel
(Placement), a tooltip, the status bar, a spreadsheet, the Start page, a
message box -- in Dark, then switched to Light and back with everything
open. Four more of the kind:

- *The navigation style icon in the status bar.* A dark mouse on a dark
  bar. `Mod/Tux` has the icons in two sets and loaded the dark strokes
  always (upstream chooses by the style sheet's file name, which says
  nothing here: both themes are `FreeCAD.qss`). It chooses by the
  indicator's own text colour now, and again when the palette changes.
  Two styles have no light icon (OpenSCAD, TinkerCAD) and keep the dark
  one.
- *A spreadsheet's text.* Black on the dark sheet. The Dark pack set the
  aliased cell's background and left `TextColor` at its default, black;
  upstream's pack sets the three text colours. Both packs set them now --
  Light too, or going back from Dark would leave light text on a light
  sheet.
- *An open spreadsheet did not follow its colours.* The model read them
  once, when the view was opened, so a theme applied afterwards left an
  open sheet in the old ones (light text on a sheet turned light, in the
  switch back). It watches the six colour preferences now.
- *Report view lines written before the theme changed* kept the colour
  they were written in: black on a view that had turned dark. The
  highlighter gives every line the colours as they are when one of them
  changes.

**Seen and left:**
- The link on the Start page's first-start panel ("Looking for more
  themes?") is a dim blue on the dark panel. The sheet gives links
  #71b6fb and the application palette has it; that one label did not
  take it in a session whose theme was applied after the page was made.
  Not looked at in a session started dark.
- `ReportOutput::OnChange` answers `colorCriticalText` by setting the
  TEXT colour. Not touched: no pack sets that key.
- What the audit did not open: the sketcher's panels, the expression
  editor, the material editor, the addon manager, TechDraw's pages and
  panels, the other workbenches' task panels, the overlay title bars with
  a dock floating, the seven legacy sheets (`Dark.qss`, `Darker.qss`,
  ...), which are not offered as themes any more. The title bar and the
  navigation icon follow the text colour and so hold for those sheets
  too; the check box edge is theirs to draw.

`tests/gui/theme-switch-contrast.py` claims all six in Dark, in
Light after it and in Dark again, by reading what the widgets paint:
39 PASS; on the copy staged 17:44, 16 FAIL (every claim made in Dark but
the presence checks). The open sheet that did not follow is not among
those sixteen -- the staged copy's packs never changed the sheet's text
colour, so it had nothing to follow; it showed once the packs set it
(`entry11-before\open-sheet-after-switch-to-light.png`).

ctest on the tree with this entry and entry 13 in it: 781 of 782, the one
being `DeferredLoad_tests_run`'s timeout, as it was before them.

## 12. TechDraw: dimensions and cosmetics are covered by the face fill -- STAGED

**Reported (2026-10-06 11:58):** "techdraw dimension/cosmetics is covered by
face filling." And, while it was being looked at: "the qt painted page covers
the dimension and cosmetics by face. it must be a rendering order problem",
"they used to work fine".

**Seen** in the dev tree on a copy of `scanner.FCStd` (the recompute question
answered No): `Page004` holds 26 dimensions and 2 balloons, every one with
`Visibility` on, and the Qt-painted page shows none of them -- nor its section
lines, nor its centre lines.

**It is not the order.** Two pictures of that page with every part view given
an opaque yellow face fill, kept as
`..\dl\handson\2026-10-06\entry12-page004-yellow-fill-repair-off.png` and
`...-repair-on.png`:
- as the staged copy has it: the dimensions that sit OUTSIDE every face (the
  7.24 beside the front view, the 7 beside the top view) are missing just like
  the ones inside, and so are the section arrows in the margin. Nothing is over
  them there.
- with the fix below: every dimension, leader and centre line is drawn over
  the yellow, also after the views are recomputed. The order is right.

**Cause.** Each of them is drawn with no opacity. A colour has four
components, and the fourth used to be a transparency that nothing in TechDraw
looked at; documents hold it as 0 (`Dimension005`: `Color (0, 0, 0, 0)`).
Since `189e3b629a` (2026-08-18, "colour conversion goes through
color_traits") `asValue<QColor>()` carries it as an opacity, so a colour
restored from such a document is a fully transparent pen. A dimension made in
this build has opacity 1 and was never affected, which is why a new page
looked right. "They used to work" is the time before that commit. The
document's faces, for what it is worth, are saved 100% transparent
(`FaceTransparency 100`) and cover nothing either way.

Upstream met the same thing and repairs it when a view provider is restored
(`ViewProviderDrawingView::fixColorAlphaValues`, preference
`FixColorAlphaOnLoad`); the fork did not have it.

**Fix.** Upstream's repair, ported: a colour property a TechDraw view
provider restores with no opacity at all reads as opaque. Two differences:
- who wrote the file. Upstream skips files of 1.1 and later. The fork's
  releases are dated (`ProgramVersion="2025.1020..."`), which reads as far
  later than 1.1 and would have skipped every file it was needed for; a dated
  version and 0.x are both repaired.
- the hatches. `ViewProviderHatch` and `ViewProviderGeomHatch` are not drawing
  views and upstream leaves them out; a geometric hatch draws its lines with
  the same colour conversion, so they get the repair too.
`Mod/TechDraw/General/FixColorAlphaOnLoad` (default on) is the way out for a
colour meant to have no opacity.

`tests/gui/techdraw-colour-without-opacity.py`: a dimension and a geometric
hatch given colours with no opacity, saved, reopened -- opaque and on the
page; with the preference off, left as stored and not drawn (which is the
test scored against the old reading).

Not looked at: cosmetic edges and centre lines of the reporter's own making.
Their colour is saved as `#RRGGBB` and takes its opacity from the preference
colour, which is opaque in the reporter's configuration; the centre lines of
`Page004` came back with the fix. If a cosmetic line is still missing after
the next stage, that is a different cause.

## 13. Report view: grouped messages (a change request) -- STAGED

**Asked (2026-10-06 11:58):** "do not use underscore in console grouped
message, intead, put a clickable expansion icon before the message (note,
those grouped message should still align with other normal message, put the
icon in front of it, in the margin area)."

Wanted: no underscore on a grouped message; a clickable expand icon ahead of
it; the message text itself stays aligned with ordinary messages, the icon in
the margin.

**Done.** A line that stands in for repeats (`... (x5)`) is no longer
underlined. Its mark is a triangle in the view's left margin, pointing at
the line while it is closed and down while its messages are shown, drawn
in the text's colour so that it follows the theme (entry 11).
- *The margin* is the document's own, widened to the height of a row of
  text. It is the one indent every line gets alike, so a line with a mark
  and a line without start in the same column, and it survives the view
  being cleared, which a format on the blocks does not. The view has the
  same margin at its top, bottom and right for it, about ten pixels more
  than before.
- *The click* is the mark's. A click anywhere on the line used to unfold
  it, which is why the line said so with an underline -- and why its text
  could not be clicked into to start a selection. The line's text behaves
  as any other line's now; the pointing hand shows over the mark only.
- The mark is painted by the view over its own text
  (`ReportOutput::paintEvent`), not by a widget beside it: a strip of its
  own would have had to guess the view's background under a style sheet.

`tests/gui/report-fold-mark.py`: six prints of one line and an ordinary
line -- one line with a count, not underlined, its text in the ordinary
line's column, a mark in the margin at its height and none at the other's,
a click on the text unfolds nothing, a click on the mark unfolds and a
second folds again. 9 PASS; on the copy staged 17:44, 4 FAIL (underlined,
nothing in the margin folded or unfolded, a click on the text unfolds).
Pictures: `..\dl\handson\2026-10-07\entry13-*.png`.

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

## 15. A Pad "up to first" gives a third result -- STAGED, fixed `1047cc0647`; the reporter's answer below (see `docs/HandsOnLog.md`)

**On hold at the reporter's word** (relayed by the build session, which the reporter said it to on staging, 2026-10-07 14:23): "skip entry 15 and 17
for now." The decision below stands recorded; nothing is to be done with it
yet.

**Decided by the reporter, 2026-10-07 13:20**, on the question the build
session left (its log, entry 15: `Pocket040` comes out at radius 12 where the
file has 13, because a negative `Fit` GREW the profile in the old build on one
oddly oriented face of `Hole007`, and shrinks it in this one as everywhere
else; "keep 12, or set `Pocket040.Fit` to +0.5 in the file"): asked to choose
between the two, the reporter chose **"Set Fit to +0.5"**. So `Pocket040.Fit`
becomes +0.5 in `scanner.FCStd`, for the 13 it was drawn with; the program's
behaviour stays as this build has it. It is a change to the reporter's own
file: who makes it, and when, was not said.

**From entry 8.** `Pad051`: `Type` UpToFirst, `Reversed`, profile `Binder033`,
base `Pocket039`. Volume as saved 3256.82; old build recomputed 3400.82; this
build 3282.67. The base and the profile are the same in both builds by volume
and by face and edge count. The old build does not reproduce the saved value
either, so the file does not say which is right. `Hole007` and `Pocket040` to
`042` sit on it and differ in step; `Pocket040` and `042` are `TwoLengths` in
the old build and read here as `Length` with `SideType` "Two sides", with the
same tool volume where the base is the same (`Pocket042`: 577.27 in both).
Not looked at further.

**Looked at (2026-10-07), this build, headless; not fixed.** It is not
`Pad051` and not "up to first" (`..\dl\handson\2026-10-07\entry15-*`,
the probes `pad15c.py` to `pad15f.py` beside their outputs):

- *The pad alone is right.* Recomputed by itself on its base as saved,
  `Pad051` comes out as saved to the last digit: tool 1792.5166, pad
  3256.8230 (`entry15-fuse-new.txt`). The old build's 3400.82 and this
  build's 3282.67 are both from a recompute of EVERYTHING, where the pad's
  base, `Pocket039`, has already gone wrong: same volume, 1464.3064, but
  no longer valid, four of its cylinder faces bounded as full circles.
- *67 of the file's 405 shapes differ from what was saved* after a
  recompute of everything (`entry15-first-invalid-new.txt`), most of them
  in face bounds only. Eight are no longer valid: `Pocket017`,
  `Boolean003` and `Reference003` on it, `Pocket037`, and `Pocket039`,
  `Pad051`, `Hole007`, `Pocket040`. The first is `Pocket017`: 352.2463 as
  saved, 444.8578 recomputed -- more than its own base, 361.0674, which a
  cut cannot give. `Pocket037` reads the same two numbers. THE OLD BUILD
  GIVES THE SAME 444.8578 for both (`2026-10-06\entry8-vol-old.txt`), so
  this is not the new kernel.
- *What is wrong in it:* two cylinder faces, of radius 7 and 5.75, run
  1.23 turns where they ran 0.23: the arc they should be, plus a whole
  turn (`u` 0 to 7.7466 for 0 to 1.4634; area 37.959 for 7.171).
  "Unorientable shape" to the kernel's check. The `Pad051` faces "a full
  turn too long" (66.787 for 3.492) are the same thing further down.
- *The same inputs give the right pocket every other way*
  (`entry15-pocket017-new.txt`, `entry15-pocket017-full-new.txt`).
  `Pocket017` is a 1 mm pocket of `Sketch041` out of `Pocket008`.
  Recomputed alone: right. Its five base features and its sketch
  recomputed one after the other, then it: right. And after a recompute of
  everything has left it wrong, its base, its sketch and its tool are what
  they were as saved (valid, same volumes, same bounds); a plain cut of
  that base by that tool, by hand, is right (352.2463, valid); and the
  pocket recomputed alone once more, on exactly what the full recompute
  left, is right again.

So the wrong pocket needs the recompute of everything AROUND it, with
inputs that measure the same. It is the same wrong number in two runs here
and in the old build's run, so it is not a race. What that leaves is state:
something an earlier feature of the full recompute leaves behind that this
cut then reads -- in the shapes it shares with them (a copy taken for the
hand cut does not carry it, and the hand cut is right), or in a cache.
`Pocket037` goes wrong by the same two numbers, so a second feature built
the same way is in it; whether it is a copy of the first was not looked
at.

**Next:** catch `Pocket017`'s cut inside the full recompute, with the
shapes as they are at that moment and not copies of them
(`SHOW_TOPO_SHAPE`, the way the kernel's other cases were taken apart),
and compare with the same cut when it is run alone. There is no case for
the kernel's issue list yet: two BREP files and a cut do not reproduce
it.

## 16. Faces of a "Mutated" binder are renamed by every recompute in a new session -- STAGED

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

## 17. `Sketch043`, `Sketch055`: "Missing external geometry reference" -- STAGED, fixed `3c8cd63032`, a question for the reporter (see `docs/HandsOnLog.md`)

**On hold at the reporter's word** (relayed by the build session, which the reporter said it to on staging, 2026-10-07 14:23): "skip entry 15 and 17
for now." What follows -- the rule, the audit, the two leads -- stays as
the record; no change is asked for yet.

**The reporter on the question the build session left** (its log, entry 17:
is a Relative binder in a moved group MEANT to come back 53 mm away),
2026-10-07 13:29: "that's how binder is supposed to work. a binder needs a
context to recompute. if the user double click the binder inside the
LinkGroup then it's context is relative to the linkgroup, which the binder
will record inside a property (either named Owner or Parent or something
else I don't remember). so without that step, binder continues to recompute
with the last set context".
The rule, then: a binder is recomputed in the context it has RECORDED. The
context becomes the link group when the user double-clicks the binder inside
that group, and is written into a property; until that is done again, the
last recorded context goes on being used. Moving with the group is right for
a binder whose recorded context is the group, and only for that one.
Read by the note-taker, from the source and from the copy of the file
(`..\dl\handson\2026-10-06\scanner.FCStd`, its `Document.xml`; nothing opened
in the program, nothing run):
- The property is `Context` (`Part::SubShapeBinder`,
  `src/Mod/Part/App/SubShapeBinder.cpp`): hidden, an object and a sub-name,
  "Stores the context of this binder. It is used for monitoring and auto
  updating the relative placement of the bound shape".
- In the file `Binder008` has `Relative` true and an EMPTY `Context` (`<XLink
  file="" stamp="" name=""/>`). It is a member of `LinkGroup001`
  (`ElementList`), whose placement is 53 mm along z. So this binder has no
  recorded context at all: the group was never made its context.
  (`Binder017`, for comparison, has one: `Body007`, sub `Misc.Binder017.`.)
- What the code does with an empty context, `SubShapeBinder::update()`: when
  `Relative` is on, no context is recorded and the context's sub-name is
  empty, it takes the binder's own parents (`getParents()`), adopts the FIRST
  one as the context and writes it into `Context`. No double-click is
  involved. For `Binder008` that first parent is `LinkGroup001`, and from
  that recompute on the binder is relative to the group -- hence the 53 mm.
So by the reporter's rule the 53 mm is not owed: this binder was never given
the group as its context, and "the last set context" is none. What moves it
is the code adopting a context by itself when none is recorded. Whether that
adoption is itself meant -- a binder with no context taking its first parent
at its first recompute, which is also how a new binder inside a group gets
one without a double-click -- or should not happen for a binder restored
from a file, is the question that is left; put to the reporter 13:35.

**The reporter's answer, and an audit asked, 2026-10-07 14:19:** "audit for
all the places binder Context is set. by right, it should record the context
the first time an object is dropped onto the binder in tree view." So the
intent: the context is recorded ONCE, when an object is first dropped onto
the binder in the tree view. Not at a recompute.
**The audit, by the note-taker, from the source only (nothing run):** the
property is written in three places and nowhere else -- no command, no Python,
no other module.
1. `SubShapeBinder::update()`, `src/Mod/Part/App/SubShapeBinder.cpp` 279-300,
   at EVERY update of a `Relative` binder, a recompute included:
   - context recorded and still leading to this binder: it is lengthened to
     the top-most parent of the context object (`parent->getParents()`, the
     first one) and written back if that changed it;
   - context recorded but no longer leading to this binder: treated as none
     for this update, left in the property as it is;
   - NO context recorded (object and sub-name both empty): the binder's own
     `getParents()` is asked, the FIRST parent is adopted and written. This
     is what gave `Binder008` the group, and it needs no action of the user.
2. `ViewProviderSubShapeBinder::updatePlacement()`,
   `src/Mod/Part/Gui/ViewProviderSubShapeBinder.cpp` 358-393, both branches
   (381, 393): the context is taken from the SELECTION -- exactly one selected
   item whose path leads to this binder gives the object and the path;
   anything else logs "invalid selection" and writes an EMPTY context. Then
   `update(UpdateForced)`, where an empty context is filled by 1. It is
   called from four places:
   - `doubleClicked()` -- the double click the reporter described;
   - `setEdit(0)`;
   - the context menu's "Synchronize";
   - `dropObjectEx()`, after the dropped links are set, whenever `Relative`
     is on -- at EVERY drop, not the first only.
3. Restore from the file (the property is saved; `Binder017` has one).
Creating a binder (`PartGui::makeSubShapeBinder`,
`src/Mod/Part/Gui/Command.cpp`) works out the container from the selection to
resolve the support, but does not write `Context`; the new binder gets one
from 1, at its first update, if it has a parent by then.
**Against the intent:**
- (i) 1's adoption of the first parent is not a drop and not the user's doing;
  it runs at any recompute, a restored file's first one included. This is
  the 53 mm of this entry.
- (ii) the drop does record a context, but at every drop, and from the
  selection rather than from where the binder was dropped on. To check when
  it is run: during a drag in the tree the selection is normally what is
  being DRAGGED, not the binder -- if so the drop writes an empty context and
  1 then adopts the first parent, so even the drop does not record what the
  reporter means.
- (iii) double click, edit and "Synchronize" re-record it too. The reporter
  described the double click as a way to set the context, so those may be
  wanted; "the first time" then applies to the drop alone. To confirm.
- (iv) nothing clears a context on purpose; it is emptied only by an
  "invalid selection" in 2.
Not decided yet: whether this audit becomes a change request (1 stops
adopting; the drop records once, from the drop target), and what a binder
with no context does at a recompute -- stays as it was built, as the reporter
said of "the last set context".

**The reporter, 2026-10-07 14:24, correcting the above and pointing at two
things to check:** "it should be every drop. so something happened that cause
the binder loose its binding. or, check if context is set when PartDesign
auto import external object as binder. maybe that's the missing point". So:
recording the context at EVERY drop is right ((ii)'s "the first only" is
withdrawn, what is left of (ii) is where the drop takes it from); and two
leads -- the binder lost a context it had, or the automatic import of an
external object as a binder never gives it one.
**Read for both by the note-taker, from the source only (nothing run):**
- *The import does not set it -- the reporter's second lead holds.*
  `Part::SubShapeBinder::import()` (`src/Mod/Part/App/SubShapeBinder.cpp`,
  from line 1385) is handed the object being edited WITH its path
  (`editObjT`: the top parent and the sub-name down to the edited object) and
  uses exactly that to resolve the support (`topParent->resolveRelativeLink(
  subname, link, linkSub, Flatten)`). Then it creates the binder, adds it to
  the container, calls `setLinks()` -- and never writes `Context`, though the
  context is in its hands. The binder gets one only from `update()`'s
  adoption of `getParents()`'s first entry, which goes by the order of the
  binder's in-list (`DocumentObject::getParents`), not by the path the user
  was editing through. Where the container is reachable one way only the two
  agree; where it is reachable several ways -- a body that is also linked, a
  part under a link group -- the adopted context can be another path than
  the one the import was resolved against.
  Its callers, all of which inherit this: `PartDesignGui::importExternalObject`
  (`PartDesign/Gui/Utils.cpp`), used by the feature commands
  (`PartDesign/Gui/Command.cpp`, five places), `ReferenceSelection.cpp`,
  `TaskSketchBasedParameters.cpp` (four places); `Part/Gui/TaskAttacher.cpp`;
  Python `Part.importExternalObject`. The binder command
  (`PartGui::makeSubShapeBinder`) is the same: it knows the selection's top
  parent and path and does not write them either.
- *`Binder008` itself more likely never had one than lost one.* The only
  thing that empties a recorded context is `updatePlacement()` on an "invalid
  selection", and that calls `update()` in the same breath, which refills an
  empty context from the first parent if there is any parent. So a binder
  saved with an EMPTY context while it is a member of a group can only have
  been updated last when it had NO parent: it was created, or last updated,
  outside `LinkGroup001` and put into the group afterwards. Dragging the
  binder into a group is not a drop ONTO the binder -- it sets no context and
  runs no update. (The other way to the same state: `Relative` switched on
  after the last update; `onChanged` connects a signal for it and does not
  update.) Deduced from the code, not from the file's history. It is not an
  "Import": it is named `Binder008`, binds the whole of `Body004`, and has
  `BindCopyOnChange` Mutated, which is what the binder command makes.
- *Where the drop takes it from,* still to be run: `updatePlacement()` reads
  the SELECTION, and wants one selected item leading to the binder.
What this adds up to, for the reporter to turn into a request or not: the
places that create a binder know its context and should record it (import,
the binder command); and a binder put into a group afterwards has none until
something is dropped on it or it is double-clicked -- at which point a
recompute should not invent one.

**Decided by the reporter, 2026-10-07 14:34**, on the two points just above,
put to them as (1) the places that create a binder record the context they
know, and (2) a recompute does not invent a context: "about the binder 1
yes. 2 recompute logic now should be fine. it only assign a new one if the
old recorded one does not exist, or empty. it must have some context in
order to be 'Relative'."
- **(1) is a change request:** `Part::SubShapeBinder::import()` and the
  binder command (`PartGui::makeSubShapeBinder`) write `Context` themselves,
  from the top parent and path they already resolve the support against,
  instead of leaving it to the first update's adoption. All of import's
  callers get it with that.
- **(2) is withdrawn: the recompute stays as it is.** A `Relative` binder
  has to have a context, so `update()` giving one to a binder that has none
  is meant. That settles this entry's question too: `Binder008` taking
  `LinkGroup001` at its first recompute, and coming out 53 mm from where the
  file has it, is the program working as designed. What follows from it in
  `scanner.FCStd` -- `Sketch043` changing, `Pad033` losing its region -- is
  the model's, to be redone in the file, not a defect.
- One case to check against the reporter's wording, since the code does not
  do quite what it says. "It only assign a new one if the old recorded one
  does not exist, or empty": EMPTY is covered, and so is a context whose
  object is gone (the link is cleared with it, which leaves it empty). But a
  context whose object still exists while its path no longer leads to the
  binder -- the binder taken out of that container -- is neither: `update()`
  drops it for that update (`parent = 0`) and, because the stale sub-name is
  not empty, adopts nothing and writes nothing. The binder is then computed
  with no context while `Relative` is on, and keeps the stale one in the
  property. By the reporter's rule that binder should be given a new context.
  Read from the source, not run.
The hold on this entry ("skip entry 15 and 17 for now", 14:23) was not
lifted with this; (1) waits with it until the reporter says.

**The reporter on that one case, and a check asked, 2026-10-07 14:40:** "by
old one, I mean the old context path. if the path, the abosolute path that
must start from top level node, does not exit, then it will assign a new
one. check that". The intent, exactly: the recorded context is a path that
starts at a top-level object; when that path no longer exists, the binder is
given a new one.
**Checked by the note-taker, in the source and by running it** (headless,
`FreeCADCmd` of the dev tree as built (the code staged 14:23), nothing built; script and
output `..\dl\handson\2026-10-07\entry17-context-stale-path.py` / `.txt`):
- *Starting at a top-level object: yes.* When the recorded path still leads
  to the binder, `update()` asks the context object for ITS parents
  (`DocumentObject::getParents()`, which returns paths from the top-level
  ancestors down) and puts the first in front of the recorded path, so a
  context recorded part-way up is made to start at the top.
- *A new one when the path no longer exists: NO.* `update()` gives a new
  context only `if(!parent && parentSub.empty())`. A recorded path that no
  longer leads to the binder sets `parent` to 0 and leaves `parentSub` as it
  was recorded -- not empty -- so nothing is adopted and nothing is written.
  Run, with a box at the origin, `PartA` at z = 10, `PartB` at z = 100 and a
  `Relative` binder of the box:

  | step | Context | binder's ZMin |
  |---|---|---|
  | made outside any container, recomputed | none | 0 |
  | put into `PartA`, document recomputed (binder not touched) | none | 0 |
  | touched and recomputed in `PartA` | `PartA`, `Binder.` | -10 |
  | moved to `PartB` (the old path is gone), touched, recomputed | `PartA`, `Binder.` -- UNCHANGED | 0 |
  | recomputed again | `PartA`, `Binder.` | 0 |
  | `Context` emptied by hand, recomputed | `PartB`, `Binder.` | -100 |
  | the context object deleted, recomputed | none (no parent left) | 0 |

  So after the move the binder keeps the stale path and is computed with NO
  context -- 0, neither the old -10 nor the -100 its new place would give --
  while `Relative` is still on. Only an empty context is replaced.
  The second row is `Binder008`'s state in the file, reproduced: a binder
  put into a container after it was made stays without a context until it is
  itself recomputed.
- **So this is a defect against the stated intent,** and a small one to
  state: the adoption has to run when the recorded path does not lead to the
  binder, not only when it is empty. It joins (1) as the second thing asked
  for under this entry, on hold with it.

**From entry 16.** With `Binder013`, `014`, `017` and `018` valid again, what
is built on them is recomputed for the first time in a full recompute of
`scanner.FCStd`, and these two sketches fail (`entry16-first.txt`). Not looked
at. One thing to check first: the four binders move 53 mm with `Binder008`
and are renamed with it, and they are not the ones whose container moved, so
the search by geometry has no motion to go by for a reference into THEM.

## 18. TechDraw pages do not load: "invalid vector subscript" -- STAGED

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

## 19. TechDraw: other indexes taken on trust (an audit asked) -- STAGED, fixed `805b5afb25` (see `docs/HandsOnLog.md`)

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

## 20. TechDraw: crash when the page is switched to the backend -- STAGED

**Reported (2026-10-06 17:53, under cdb):** "there is a crash when I started
bgfx rendering of techdraw". The copy staged 17:44, a page open and painted
by Qt, `Mod/TechDraw/General/PageRendererVg` switched on.

**Seen** (`tools\dbg\cdb_fcad_user.log`, first chance): an access violation
reading 0x8 in `QOpenGLContext::isOpenGLES`, from
`QOpenGL2PaintEngineEx::renderHintsChanged`, from a `QGraphicsSvgItem` being
painted, inside the first paint of a `QOpenGLWidget` that
`QGVPage::setRenderer` had just made the viewport. Two lines before it:
"bgfx is already running on Direct3D 11 in this process; OpenGL 2.1 needs a
restart to select" and Qt's `Warning: "" failed to compile!`.

**Cause.** `QOpenGLContext::currentContext()` was null in the middle of the
page's paint. The page layer wanted its zero-copy composite, which needs the
backend's device on OpenGL in Qt's share group: it swapped the viewport for
a GL one, and in that viewport's first paint called
`RendererFactory::warmup("bgfx - OpenGL", viewport)`. On this session the
device was already up on Direct3D 11 (the Windows default), so the warm-up
could change nothing -- but it still made the device's own Qt context
current, pumped a frame and called `doneCurrent()`. The painter that Qt had
open on the viewport was left with no context: its next shader would not
compile, and the item after that dereferenced the null.

**Reproduced** in the dev tree with a copy of the reporter's `user.cfg`
(`PageRendererVg` is 0 in it: the switch was made in the running session),
a box, a page, a view, the preference set from a timer: the same fault
address (`Qt6Gui.dll+0x39d896`), the same two lines before it.

**Fix.**
- *The renderer.* `BGFXRendererLib::warmup(QOpenGLWidget*)` gives the caller
  its GL context back on every way out.
- *The page.* Whether the composite can engage is settled before the
  viewport is touched. A device that is up and not OpenGL never will share,
  so the page keeps its raster viewport and reads the layer back. Only a
  session with no device at all is warmed up, and from the main window's
  warm-up surface, not from a viewport that dies with the page.
- *Switching.* The page view watches the two preferences and repaints.
  Switched off, it drops the layer and goes back to a raster viewport with
  its background cached, as the constructor left it; the composite switched
  off alone takes it off the GL viewport too.

**Then what it drew**, once it drew at all (found by looking at the page and
by differencing it against the Qt-painted one, the box page first and
`scanner.FCStd` after):
- The read-back layer was opaque white: the grey backdrop and the sheet's
  outline were painted over. `Page2D::renderOffscreen` takes `transparent`
  now, the page asks for it and for device pixels (it ignored the pixel
  ratio).
- The sheet's outline came out light grey and a pixel off with the
  background cache off: the pen and the antialiasing were whatever the
  painter arrived with. Both are set now.
- Vertex dots twice the size of Qt's: `QGIVertex::setRadius` draws an
  ellipse as wide as its argument.
- On `scanner.FCStd`, `Page`: arcs sweeping across the whole sheet. The feed
  drew an arc of circle from `AOC::startAngle`/`endAngle`, which are the
  curve's parameters, measured from the circle's own X axis; it takes the
  angles from the arc's start, middle and end points now, as the Qt tier
  draws from the end points.
- Same page: Bottom, Front and Top drawn a second time at the bottom left
  corner of the sheet. They are items of a projection group, whose X/Y are
  relative to the group. `PageFeed::pagePosition` adds the group's, and the
  two damage checks (the page view's and the browser page's) compare it, so
  a moved group re-feeds its items.

**Measured**, this build, 668 x 630 viewport, Direct3D 11, a full paint of
the page: the box page 1.8 ms Qt-painted, 4.7 with the layer; the four
pages of `scanner.FCStd` taken, 3.0 to 7.3 ms Qt-painted and 5.6 to 10.1
with the layer. Switching back gives the Qt-painted page pixel for pixel
(0 of 420840) on all four. An OpenGL session (`FC_BGFX_D3D11=0`) composites
in a GL viewport ("vg compositor active") and switches back the same; its
picture was read with `glReadPixels` and is right, with the Qt items
aliased -- a GL viewport has no multisampling here.

`tests/gui/techdraw-page-backend-switch.py`, 11 PASS on a fresh
configuration, on the reporter's and on an OpenGL session. Scored: the same
steps crashed before the first two fixes (no DONE line), and with the arc
and the group position put back as they were the test FAILS "no ink away
from what Qt draws" (495 pixels), both together -- not one at a time.

**The double draw (said by the reporter, 2026-10-06 evening).** With the
layer on, the page was drawn TWICE: the backend's layer underneath, and
every Qt item on top of it, as before -- what `PageRendererVg` was built as
("the verification tier ... not yet its interactive integration"). Text and
the template looked bolder for it (the same strokes blended twice). Asked
what "drawn by the backend" has to mean, the reporter: "next session, fix
the double draw first", and, to "the backend's picture is the one that
stays; the Qt items stop painting what the layer covers while still taking
the mouse": "yes qt no longer draw when backend takes over".

**Fixed** (docs/TechDrawPortAndSection.md section 37 has the design):
- The page view takes the painting of the scene's items into its own hands
  while the layer is on, and paints only what the layer does not hold: the
  template's fields, a clip group and what is in it (which also settles
  the clip group's views: Qt's, whole), a tracker. The items stay in the
  scene, so hover, selection and drag go on as before.
- The layer shows what only the Qt items used to: a view's frame, label,
  caption and lock; what is preselected or selected (fed again when the
  scene says something changed, with no geometry computed); the frame of
  a theoretically exact dimension, a leader text's box.
- Two differences that the Qt items had been painting over: a view's
  frame was 0.35 mm wide, five pixels when zoomed in (Qt draws a
  one-pixel hairline at any zoom), and lines narrower than a pixel all
  but vanished (the 2D engine faded them with the square of their
  width). The engine now never draws a stroke narrower than a device
  pixel.
- `Mod/TechDraw/General/PageRendererVgVerify` paints the Qt items over the
  layer as before, to compare the two pictures by eye.

**Measured**, the four pages of `scanner.FCStd`, a full paint in ms,
Qt-painted / the layer alone / both as it was: 6.3 / 5.2 / 10.8,
3.6 / 4.5 / 7.9, 3.5 / 5.6 / 6.5, 8.3 / 6.6 / 17.2. Of 162 to 872 items a
page, Qt still paints 25 (the template's fields).

**The mouse**, by events sent to the page view, the same steps with the
layer on and off: hovering an edge, clicking it, clicking the sheet,
pressing on a face give the same selection at every step and the
highlight colours on the page in both; the view dragged by its label ends
at the same X/Y with its picture there.

`tests/gui/techdraw-page-backend-single-draw.py`, 17 claims (the picture
against the Qt page's both ways, the verify switch as the control that
"drawn once" can fail, selection and preselection shown and gone pixel for
pixel, nothing fed while idle, a moved group).

**What the backend's page still does differently**, for the reporter to
weigh when looking at it: text in a font TechDraw does not ship comes out
in osifont; the template is a raster and stops sharpening far zoomed in; a
frame's dashes grow with the zoom; vertex dots are a little smaller.
Not done: antialiasing for what Qt still paints in a GL viewport.

**The reporter's two earlier notes on this option, as taken** (15:56 and
16:19; what they describe is entry 18's exception, thrown in the page's
paint and at load, and the crash above):

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

## 21. TechDraw: a click on a section line starts a section; the line shifts -- STAGED

**Reported (2026-10-06 15:21):** "clicking a section line in techdraw page
will trigger a section operation even without moving the section line. also
the selection line position will be shifted each time the section is
recomputed". ("selection line" read as the section line.)

**Reproduced** by `tests/gui/techdraw-section-line-click.py` on the build
staged 17:44: a base view at 2:1, a section 4 mm off the centroid
(`SectionOrigin` y = 14, the centroid at 10). One click on the line, the
mouse not moved: `SectionOrigin` y = 18, and "Move section line" on the
undo stack. A drag of 10 mm up the page: y = 36 where 19 was meant.

**Two causes, both in the fork's section line dragging.**
- *The click.* `QGISectionLine::onItemMoved` ended every release of the
  mouse on the line in "Move section line": the section was given the
  line's points again and the document recomputed. Nothing asked whether
  the line had moved. The same for a change point mark ("Rotate section
  line").
- *The shift.* The points go to `DrawViewSection::setChangePoints` as the
  base view draws them, times the view's scale; it took them for unscaled.
  It also compared them with the present line's middle projected a second
  time, which never matched, so the origin was written every time. Each
  call therefore took a section that is not on the centroid away from it
  by the scale again: 4 mm, 8, 16. At 1:1, or with the section through the
  centroid, nothing showed.

**Fix.**
- A release is a move only if the mouse went as far from where its button
  came down as starts a drag anywhere else
  (`QApplication::startDragDistance`); short of that the line, or the
  mark, goes back and the section is left alone.
- `setChangePoints` divides by the base view's scale, compares with the
  line's middle as `sectionLineEnds` gives it, and moves the origin BY
  what the middle moved, in the base view's plane -- what the origin has
  along the view's direction stays.
- A line that shows no marks at its ends (`SectionLineMarks` off) has no
  change points, and the drag read past the end of an empty vector; it
  moves by its two ends now.

`tests/gui/techdraw-section-line-click.py`: 10 PASS. On the build staged
17:44, 6 of its first 9 claims FAIL (the two origins above). With the
second fix alone a click moves nothing, since the points it hands over are
the ones the section has -- and is still carried out: the claims that a
click recomputes nothing FAIL (3 recomputes for 3 clicks) until the first
fix is in.

Not looked at: rotating the line by a mark (the direction it gives the
section), beyond leaving a click on a mark alone.

## 22. Omni search: `/word` with no space is an object query (a change request) -- STAGED, fixed `5aedd5cf83` (see `docs/HandsOnLog.md`)

**2026-10-06 15:24, omni search (a change request).** "omni search first
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

## 23. Omni search: the settings it collects (an audit asked) -- STAGED, fixed `c7a27b5a85`; the defaults FIXED `02cab053df`, not staged (see `docs/HandsOnLog.md`)

**For the reporter, from the build session** (passed on by the build session, 2026-10-07 15:51), two things:
- their own profile carries the clamped values -- overlay delays 99 for 200,
  wheel delay 99 for 1000, `CyclesSamples` 99 for 256, and more (seen in the
  copy of the profile of 2026-10-07 00:13). The fix stops OK from writing
  them; nothing puts a stored value back. The log, entry 23, lists the keys
  and their defaults.
- a question: the Selection preference page showed the two highlight colours
  as spin boxes reading 99. They were taken off that page rather than given
  colour buttons there, because the Colors page has both and the page saved
  last would undo the other's change. Are colour buttons wanted on the
  Selection page anyway? NOT ANSWERED YET.

**The reporter on the audit's side findings** (relayed by the build session, which the reporter said it to on staging, 2026-10-07 14:23): "entry 23, yes
fixed the defaults, leave the unused ones." The defaults that disagree with
their preference page are to be fixed; the settings nothing reads stay.

**2026-10-06 15:28, omni search: the settings it collects (an audit asked).**
"audit for all parameter/preference settings auto collected by omni search.
ensure all settings has documentation, but not overly long. screen for those
long ones that you may mistakenly added for development purposes". Asked
for: (a) go through every parameter/preference setting the omni search
collects automatically; (b) each must have documentation; (c) none of it
overly long; (d) pick out the long ones in particular -- text an agent wrote
as development notes that ended up as a setting's documentation.

## 24. Every setting behind a generated helper class, applied by delayed handlers (a change request) -- OPEN

**Next for the build session, at the reporter's word** (to it, 2026-10-07,
asked which entry "do entry 23 next" meant): "I meant entry 24, but have
you finished 23?"

**2026-10-06 15:51, every setting behind a generated helper class, so the
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

## 25. The outline of a highlighted face is jagged, MSAA or not -- OPEN

**From the build session, a datum and no more** (passed on 2026-10-07 17:05):
MSAA was not in effect at all when this was reported (entry 39). With MSAA
really on, the highlighted face's outline is STILL jagged, on its inner
edge: it is a stencil cut in a pass drawn after the resolve (pictures in
`..\dl\handson\2026-10-07\entry25-outline\`). So this entry stands on its
own; "MSAA or not" was partly entry 39.

**2026-10-06 15:44, face highlight edge is jagged.** "face highlight
silhouette shows jagged edge regardless whether msaa is used or not". The
outline of a highlighted face is aliased, and switching MSAA on or off makes
no difference to it. Not said yet: whether this is the hover highlight, the
selection highlight or both, and which document.

## 26. A long halt after enabling MSAA and pressing OK -- FIXED `175ffce199`, not staged (see `docs/HandsOnLog.md`)

**The reporter on what was found** (relayed by the build session, which the reporter said it to on staging, 2026-10-07 14:23): "entry 26 is probably
not the view provider, because the delay I experience is longer. most likely
related to stylesheet re-apply." The halt is longer than the 1.07 s reload
the build session measured; the stylesheet being applied again is where the
reporter expects it. To be looked at from that side, with entry 30's freeze.

**2026-10-06 15:48, a long halt after enabling MSAA and pressing OK.**
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

## 27. The view cell menu: a spreadsheet nobody asked for, every TechDraw object listed, the wrong cell changed -- STAGED, fixed `fa2ada985c` (see `docs/HandsOnLog.md`)

**2026-10-06 16:05, the view cell menu opens a spreadsheet nobody asked
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

**2026-10-06 16:08, the view cell menu of a TechDraw page lists every
TechDraw object.** "if techdraw page is on one view, and I click 'view cell
menu' I can see all techdraw objects listed in the menu, like 'Page',
'Detail', 'Dimension', etc." The menu that should offer what a cell can show
lists the page's own child objects -- detail views, dimensions and the like
-- next to the page, as if each could be shown in a cell. Same menu as the
16:05 note above; possibly the same list being built too widely.

**2026-10-06 16:09, the view cell menu changes the wrong cell.** "with two
3d view and one spreadsheet view side by side. I click 'view cell button' on
one of the 3d view and select a techdraw page, the spreadsheet view switched
to techdraw." Three cells side by side, two 3D views and a spreadsheet; the
view cell button of one of the 3D views is used to choose a TechDraw page;
the SPREADSHEET cell becomes the TechDraw page, not the 3D view whose button
was pressed. Third note on this menu (16:05, 16:08): here the choice is
right and the cell it lands in is wrong. Not said yet: which of the two 3D
views, and which cell was the active one at the time.

## 28. `scanner.FCStd` restores with a wrong colour, sometimes -- OPEN

**2026-10-06 17:56, `scanner.FCStd` restores with a wrong colour,
sometimes.** "the scanner file restore sometimes got wrong color, I am
seeing the motor body light grey part is showing light blue". A part that is
light grey in the file -- the motor body -- comes up light blue; not on every
load. Not said yet: the object's name, how often, and whether the colour is
wrong in the 3D view only or in the property editor too.
Read from the session's report log by the note-taker (copy:
`..\dl\handson\2026-10-06\wrong-color-report-view-1756.log`; nothing run):
this is the copy staged 17:44 (`f7d3aa0cf2`), session started 17:50:19,
`scanner` loaded 17:50:31. The "deferred view provider restore aborted ...
320 objects fall back to defaults" line of the 16:19 note is NOT in this
log, and there is no caught exception in it at all -- so this wrong colour
is not that abort. It is a load that reports nothing wrong and still shows
a colour the file does not have. "Sometimes" points at something that
depends on order or timing in the load rather than on the file.

## 29. View cells: frames that show a split, a join and a resize while it is dragged; a minimum cell size (a change request) -- OPEN

**2026-10-07 09:32, the view cell's handles, resizing and a minimum size (a
change request).** "I want change the view cell UI. replace the top right
corner handle to a close button for closing the view. keep the bottom right
handle for resizing. when resizing (either dragging the corner or the split
handle) show transparent box of the involved cell to track the resizing in
real time, just like how overlay widget does it. add a setting for minimum
size (for both width and height) default to 200, if creating a new view will
result in any existing (or the new) view fall below the limit, the view
creation is refused, show an message in console (don't flood it)." Asked
for, four things:
(a) the handle in a cell's top right corner becomes a close button that
closes the view;
(b) the bottom right handle stays, for resizing;
(c) while resizing -- by the corner or by the splitter between cells -- a
transparent box over each cell involved follows the new size live, the way
the overlay dock widgets show a drag;
(d) a setting for the minimum cell size, one for width and height both,
default 200; a new view that would leave ANY cell under it, an existing one
or the new one, is refused, with a message in the report view that does not
repeat itself into a flood.
Read from the source by the note-taker, nothing changed: the cell's corner
zones are `ViewAreaZone` in `src/Gui/ViewArea.cpp`, and its enum has two
corners, `TopRight` and `BottomLeft` -- there is no bottom RIGHT zone today.
To ask the reporter: is the one to keep the existing bottom left zone, or is
it to move to the bottom right? The overlay's live box is `OverlayDragFrame`
in `src/Gui/OverlayWidgets.cpp`, and its size floor is the setting
`DockOverlayMinimumSize` (`OverlayParams`) -- the precedent for (c) and (d).
**Revised by the reporter after the facts below were put to them, 10:19,
and this is the one to do:** "that's not very intutive without the
transparent frame I propose. so leave the close button I requested. just
make the frames right to hint the operation is enough I think".
The facts: the two corner zones, top right and bottom left, are identical
and neither is a resize handle. A drag INWARD from either splits the cell
-- a new view, beside or stacked by the drag's dominant axis -- and the rest
of the drag sizes the new border. A drag OUTWARD into a neighbour arms a
join: the neighbour dims with an arrow and is closed on release. A view is
closed from its own cell only through the cell menu ("Close view") or the
border's right-click menu; resizing is the border alone
(docs/SplitViews.md sec 5.4).
What is asked now:
- (a) and (b) are WITHDRAWN: no close button, the corner zones stay where
  they are and keep doing what they do (the note-taker reads "leave the
  close button" as "leave it out"; to confirm).
- (c) is the heart of it, and wider than resizing: every drag of a corner
  zone or of a border shows transparent frames over the cells involved, live,
  and the frames have to SAY which operation is under way -- a split shows
  the two cells the one will become, a join shows the neighbour going and
  the cell that takes its room, a resize shows the cells either side of the
  border at their new sizes. Made exact by the reporter, 10:22: "border
  resize shall track the sizes of all involved cells" -- EVERY cell whose
  size the drag changes gets its frame, not only the two that touch the
  border where the cursor is: a border with several cells stacked along one
  side moves them all, and so does one that pushes on further cells once a
  neighbour has reached its limit. It is the frames that make the gestures
  readable; today a split or a join announces itself only once it has
  happened or by a dim and an arrow.
- (d) stands: the minimum cell size setting, default 200 for width and
  height, a view creation that would put any cell under it refused, one
  quiet line in the report view.
- (e), added by the reporter 11:01: "view cell handle should draw with opaque
  background otherwise they are practically invisible in many cases". The
  corner handles are drawn as a few strokes straight onto whatever the cell
  shows; over a busy or like-coloured view they cannot be seen. They get an
  opaque background. From the source (`ViewAreaZone::paintEvent`,
  `src/Gui/ViewArea.cpp`): two diagonal lines in the palette's highlight
  colour, at alpha 130 when only hinted, nothing behind them, and nothing at
  all until the zone is hovered or hinted. Whether the menu button in the
  top left corner (`ViewAreaMenuButton`) is meant too was not said.
  Said by the reporter, 15:24: "for view cell handle opaque issue, add that
  the cell menu should also show opaque when hovering." So the cell menu
  button in the top left corner gets the opaque background too, WHEN
  HOVERED -- it may stay subtle at rest, as it is today.
- (f), added by the reporter 11:30: "make the view cell splitter thinner".
  The border between two cells is to be thinner than it is. How thin was not
  said. From the source: `ViewAreaSplitter` (`src/Gui/ViewArea.cpp`) is a
  plain `QSplitter` that never calls `setHandleWidth`, so its border has the
  width the style or the stylesheet's `QSplitter::handle` rule gives every
  splitter in the program; making this one thinner means a width of its own,
  and the border still has to be wide enough to grab and to right-click (its
  menu closes a neighbouring view).

## 30. The dark and light overlay stylesheets integrated into the Dark and Light preference packs (a task asked) -- OPEN

**2026-10-07 10:58, a task.** "add task to integrate dark and light overlay
stylesheet into dark and light preference pack".
Read from the source by the note-taker, nothing changed, and it leaves a
question open:
- The two packs are one file each, `src/Gui/PreferencePacks/Light/Light.cfg`
  and `.../Dark/Dark.cfg`, and each ALREADY names an overlay stylesheet:
  `OverlayActiveStyleSheet` is `Light_overlay.qss` in the one and
  `Dark_overlay.qss` in the other, next to `StyleSheet` = `FreeCAD.qss` (the
  parameterized sheet both themes share).
- The overlay sheets are separate files in `src/Gui/Stylesheets/overlay/`,
  and there are more of them than the packs use: `Light_overlay.qss`,
  `Dark_overlay.qss`, `Darker_overlay.qss`, `Light-Modern_overlay.qss`,
  `Dark-Modern_overlay.qss`, and beside them `Light.qss`, `Dark.qss`,
  `Light-off.qss`, `Dark-off.qss`, `Light-Outline.qss`, `Dark-Outline.qss`,
  `SplitDark.qss`.
- With no `OverlayActiveStyleSheet` set, the overlay picks
  `Light-Outline.qss` or `Dark-Outline.qss` by the look of the main sheet
  (`detectOverlayStyleSheetFileName` in `src/Gui/OverlayManager.cpp`).
To confirm with the reporter, since a pack naming an overlay sheet is there
already: is the task (1) to have the packs name a DIFFERENT pair of overlay
sheets -- which? -- or (2) to fold the overlay styling into the theme itself,
so that the overlay takes its colours from the pack's style parameters like
the rest of `FreeCAD.qss` instead of from a qss file of its own, or (3)
something seen on screen that is wrong with the overlay under the two themes?

**Added by the reporter, 11:02, a second task under this entry:** "also about
entry 30, investigate the application long freeze time when applying overlay
stylesheet". Applying an overlay stylesheet freezes the application for a
long time; find out where the time goes. How long, from the reporter
(11:04): "freeze for about several tens of seconds". How it was applied
(11:09): "the stylesheet is applied through Tools -> Preset configurations ->
Overlay dark theme". Not said yet: how many documents, views and docked
panels were open.
So it is a PRESET, not one stylesheet setting: `Std_CmdPresets` ->
`PresetsAction::applyPreset` (`src/Gui/Action.cpp`) takes the parameter set
`data/settings/OverlayDark.FCParam` and inserts the whole of it into the user
parameters in one call (`param->insertTo(manager)`). Every key in that file
is written one after another, and every observer of every one of them reacts
on the spot -- the overlay refresh above among them, once per key it watches.
The file is the list of what gets written; counting its keys against the
observers is where to start.
Read from the source by the note-taker, nothing run or timed -- where to
start: a change of `OverlayActiveStyleSheet`, of `StyleSheet` or of
`ColorScheme` in `Preferences/MainWindow` each calls
`OverlayManager::instance()->refresh(nullptr, true)`
(`OverlayStyleSheet::OnChange`, `src/Gui/OverlayManager.cpp`), with no delay
and no merging of the three -- a theme switch writes all three, so the
refresh may run three times in a row. A preference pack being applied writes
them one after another the same way. The sheet is read from its file each
time (`OverlayStyleSheet::update` -> `loadFromFile`), then set on the overlay
widgets, and a `setStyleSheet` re-polishes every child of the widget it lands
on -- tree, property editor, report view with all their rows. Entry 26 (the
halt after OK in the preferences) and entry 24 (apply a changed setting
through a delayed handler) are the same family.

**Added by the reporter, 11:34, a third task under this entry** (their choice
of place, 11:36: "maybe group this under the theme merge entry"): "add python
console stylesheet setting to dark and light theme. right now it seem to only
appear in overlay theme, which once applied there is no way to un-apply it
even switching to classic theme". Two things in it: (a) the Dark and Light
themes are to carry the Python console's styling themselves; (b) as it is,
the styling comes only with an overlay theme preset, and once that has been
applied nothing takes it away again -- not even the classic theme.
Read from the source by the note-taker, nothing changed, and it accounts for
(b) in full:
- The styling is one key, `Background`, in `Preferences/Editor`.
  `PythonConsole::OnChange` (`src/Gui/PythonConsole.cpp`) turns a non-zero
  value into a stylesheet on the console, `Gui--PythonConsole {background:
  #rrggbb}`, and a zero or missing one into no stylesheet at all.
- Only the two overlay presets write it: `data/settings/OverlayDark.FCParam`
  (`3368601600`, 0xC8C8C800) and `OverlayLight.FCParam` (`4042321920`,
  0xF0F0F000).
- `src/Gui/PreferencePacks/Dark/Dark.cfg` and `.../Light/Light.cfg` both have
  an `Editor` group -- text and syntax colours -- and neither has `Background`
  in it. A pack only writes the keys it lists, so applying any theme after a
  preset leaves the preset's background where it is. That is the "no way to
  un-apply": no theme owns the key, the classic one included.
- What does take it away today: the preset's own revert (Ctrl held while
  choosing it in Tools > Preset configurations, `PresetsAction::onAction`),
  or setting the key to 0 by hand.
So (a) is `Background` added to the `Editor` group of both packs, with the
colour each theme wants, and a value (0, for "none") in whatever the classic
theme applies, so that every theme sets or clears it. Worth the same look for
the preset's other keys that no pack lists -- `TreeView` (`TreeEditColor`,
`ItemBackground`, `TreeActiveColor`), `View` (`BackgroundColor`, `Gradient`,
`Simple`, `CursorCrosshairColor`): each is left behind the same way. This is
the first task of this entry seen from the other end.

## 31. Report view: "Go to end" on by default (a change request) -- FIXED `47b5e72c79`, not staged (see `docs/HandsOnLog.md`)

**2026-10-07 11:21, a change request.** "make console 'go to end' by
default". The report view's "Go to end" option -- follow the newest line as
output arrives -- is to be ON unless the user has switched it off.
Read from the source by the note-taker, nothing changed: the option is
`gotoEnd` in `ReportOutput` (`src/Gui/ReportView.cpp`), initialised `false`
in the constructor, toggled from the options menu ("Go to end",
`onToggleGoToEnd`) and stored as the boolean `checkGoToEnd` in the report
view's own parameter group. A profile that has the key keeps what it says;
the default only reaches a profile that never toggled it. The key is read
straight from the parameter group, not through `ReportViewParams`, so the
omni search does not list it (entry 24).

## 32. Sub menus that are transparent with blue text; transparent menus off by default -- FIXED `b960092ea5`, not staged; a question for the reporter (see `docs/HandsOnLog.md`)

**From the build session** (passed on by the build session, 2026-10-07 16:20), the answer to (a) and a question:
- Why Tools > Command history was see-through: the menus given the style
  are single menu objects shared between a pop-up over the 3D view and an
  entry of the main menu -- command history, select-up, the tool bar menu,
  camera binding -- and with no menu sheet chosen a themed session took the
  see-through sheet. The blue text is that sheet's `palette(bright-text)`,
  which under a dark colour scheme on Windows is the desktop's accent colour
  (#a6d8ff).
- What it is now: no menu sheet chosen means an ordinary menu; the
  see-through sheets are a choice in Preferences > Theme.
- The question: the 3D view's OWN pick menus are opaque by default now too,
  as "disable transparent menu by default" reads. Should those alone stay
  see-through? NOT ANSWERED YET.

**2026-10-07 11:24, a check asked and a change request.** "check why some sub
menu a transparent with blue text, e.g. Tools -> Command history. I remember
only 3d view context menu is supposed to have that property. Anyway, disable
transparent menu by default". Two things: (a) find out why sub menus such as
Tools > Command history are transparent with blue text, when the reporter's
memory is that only the 3D view's context menu was meant to be; (b) whatever
(a) turns out to be, transparent menus are OFF by default.
Read from the source by the note-taker, nothing changed:
- The look is one function, `setupMenuStyle(QWidget*)`
  (`src/Gui/Selection/SelectionView.cpp`): it sets a menu stylesheet on the
  widget it is given, taken from the `MenuStyleSheet` key in
  `Preferences/MainWindow`, or, the key being empty, `qssm:Dark.qss` or
  `qssm:Light.qss` by the colour scheme in effect (`qssm:Default.qss` with no
  main stylesheet at all). The files are `src/Gui/Stylesheets/menu/`. Both
  preference packs set `MenuStyleSheet` to an empty string, so a themed
  profile gets the Dark or Light menu sheet.
- A menu has it because the code calls that function on it, twelve calls in
  all: three in `SelectionView.cpp` (the 3D view's pick and context menus --
  the ones remembered), one in `OmniSearchBox.cpp`, and eight in
  `src/Gui/Action.cpp`, among them the command history's menu
  (`CmdHistoryAction::addTo`). So Tools > Command history is transparent
  because it was given the style on purpose, not by a stylesheet leaking;
  which of the eight are wanted is for (a) to list.
- There is no switch for it today: the function always applies a sheet.
  (b) needs one -- off unless asked for -- or the default sheet made opaque.

## 33. A cmd window pops up briefly at the first document opened after start -- FIXED `ef4df215b5`, not staged (see `docs/HandsOnLog.md`)

**From the build session** (passed on by the build session, 2026-10-07 16:20): the candidate below is the cause,
watched happening -- at the first document, which brings the first 3D view,
`FreeCAD.exe` starts `cmd.exe /c where nvcc` with a console of its own, from
the path tracer's CUDA probe (`cuew.c`, `popen`). Once per session; a second
document starts nothing. The fix goes into the cycles submodule (the PATH
searched without a shell).

**2026-10-07 14:43, a defect.** "when the application starts, the first open
of a document briefly pops a cmd window. subsequent opening of document does
not have this". Once per session, at the first document opened: a console
(cmd) window appears for a moment and goes. Not said yet: whether it is every
start, which document, and whether a NEW document does it as well as an
opened one.
Read from the source by the note-taker for where to look, nothing run -- a
candidate, not a finding:
- A process with no console that runs a command through the C runtime's
  shell -- `_popen()`, `system()` -- gets a visible `cmd.exe` window for as
  long as the command runs. FreeCAD's own launches go through `QProcess`,
  which asks for no window; the vendored path tracer does not.
- `src/3rdParty/cycles/third_party/cuew/src/cuew.c` looks for the CUDA
  compiler with `popen("where nvcc", "r")` (`popen` is `_popen` on Windows)
  and asks its version with another `popen`; `.../hipew/src/hipew.c` does the
  same for `hipcc`. Both run when the path tracer's devices are first probed,
  and the answer is kept, so it happens once in a session.
- The devices are probed when the first 3D view comes up: the report view of
  the 2026-10-06 runs has "HIPEW initialization failed: Error opening HIP
  dynamic library" at exactly that point (`..\dl\gt-open-A-usercfg-command\`
  `run.log`, between two "bgfx: view init" lines). A first document is what
  brings the first 3D view.
To establish: that it is this and not something else started at a first
open (which call, by watching for a `cmd.exe` child of `FreeCAD.exe`), and
whether a session with no 3D view opened -- a TechDraw page or a spreadsheet
alone -- shows it.

## 34. TechDraw's preselection colour sometimes does not follow the theme -- FIXED `3d7b4c30fd`, not staged (see `docs/HandsOnLog.md`)

**From the build session, as information** (passed on by the build session, 2026-10-07 15:51): Classic is not the
only way `Mod/TechDraw/Colors/PreSelectColor` gets set -- the first OK in the
preferences stores it too, as the TechDraw Colors page's own default. So a
profile can hold the key without Classic ever having been applied. The
decision below (Dark and Light set the key) covers that case as well.

**2026-10-07 15:15, a defect.** "the TechDraw preselection highlight color
'sometimes' does not follow the settings. when I switch between classic and
dark/light theme. the 3d view pre-selection change between yellow and blue,
but techdraw sometime stays as yellow. sometimes it is blue". Switching
between the Classic theme and Dark or Light, the 3D view's preselection
colour follows -- yellow under Classic, blue under Dark and Light -- and the
TechDraw page's does not always: it stays yellow at times and is blue at
others.
Read from the source by the note-taker, nothing run -- it accounts for
"sometimes" without anything being random:
- TechDraw's colour is `Preferences::preselectColor()`
  (`src/Mod/TechDraw/App/Preferences.cpp`): the key `PreSelectColor` in
  `Mod/TechDraw/Colors` if it is set, and ONLY IF IT IS NOT, the 3D view's
  `HighlightColor` from `Preferences/View`.
- The Classic pack sets both: `View/HighlightColor` = `3789624575`
  (0xE1E114FF, yellow) and `Mod/TechDraw/Colors/PreSelectColor` =
  `4294902015` (0xFFFF00FF, yellow).
- The Dark and Light packs set `View/HighlightColor` = `327679` (blue) and do
  not mention `PreSelectColor` at all.
- So in a profile where Classic was never applied the TechDraw key is unset,
  TechDraw follows the 3D view, and Dark or Light gives blue. Once Classic
  has been applied the key holds yellow, and Dark or Light, which do not own
  it, leave it there: the 3D view goes blue and TechDraw stays yellow, for
  good. Which of the two the reporter sees depends on whether Classic has
  been through that profile, not on the switch just made.
The same shape as entry 30's third task (a key one theme writes and the
others do not own). The repair is one of two, for the reporter or the build
session to choose: Dark and Light set `PreSelectColor` too, or Classic stops
setting it so that TechDraw follows the 3D view under every theme. To run
when it is looked at: that an open page picks a changed colour up without
being reopened.

**Decided by the reporter, 2026-10-07 15:19**, asked which of the two: "dark
and light set techdraw". So the Dark and Light packs get
`Mod/TechDraw/Colors/PreSelectColor` themselves, with the colour each wants
for it -- the blue they give the 3D view, unless the reporter says another --
and Classic keeps its own. Every theme then owns the key and a switch in
either direction changes it.

## 35. TechDraw: now and then a click starts a recompute, a dimension that cannot be selected; an audit for unnecessary recomputes -- OPEN

**2026-10-07 15:21, a defect, three symptoms the reporter thinks are one.**
"I open scanner file and click recompute, which has some recomputation error.
that's expected. then when I single click anything in some techdraw page
(Page003), it triggers recompute for some reason. also, click some dimension
does not register as a selection (e.g. Dimension134). I think these two might
be related. because when I select in the tree of this Dimension134 item, it
can also sometimes trigger recompute and then clear my selection."
The steps: open `scanner.FCStd`; Recompute, which ends in errors (expected:
entry 17's `Pad033` and the fillet). Then, in the TechDraw page `Page003`:
(a) a single click on anything starts a recompute;
(b) a click on some dimensions, `Dimension134` for one, does not select it;
(c) selecting `Dimension134` in the TREE sometimes starts a recompute too,
and the selection is then gone.
The reporter's reading: (a) and (b) are related, (c) being why.
Read by the note-taker from that session's report log (copy:
`..\dl\handson\2026-10-07\entry35-report-view.log`, the copy staged 14:23,
session started 14:36) and from the source; nothing run:
- The log has seven "Recompute failed!" between 15:07:55 and 15:20:16 --
  15:15:15, 15:15:32, 15:15:36, 15:16:34, 15:20:08, 15:20:16 after the first
  -- and each fails the same way: "FeatureDressUp.cpp(143): Invalid edge
  link: ?Edge93" and "Failed to recompute scanner#Pad033: Sub shape not
  found: scanner#Sketch043.?InternalFace2". So the clicks do run a recompute
  of the document, and since the two objects in error stay to be recomputed,
  each one is the whole failing recompute again. In a document without
  errors the same recompute would find nothing to do and pass unnoticed --
  the errors make an existing recompute-on-click visible rather than cause
  it.
- Where a click in a page can end in a recompute, in the source
  (`Gui::Command::updateActive()`): `QGIViewDimension::datumLabelDragFinished`
  (`src/Mod/TechDraw/Gui/QGIViewDimension.cpp` 700-714) writes the label's X
  and Y and recomputes when a drag of a dimension's label "finishes";
  `QGIViewBalloon.cpp` 516 the same for a balloon; `QGISectionLine.cpp` 685
  for a section line. Entry 21 was this very thing for the section line -- a
  click taken for a finished drag -- and was fixed there; the dimension label
  and the balloon are the same shape and were not part of it.
- That would give (a) for clicks on dimensions and balloons, and (b) with
  it: a recompute redraws the page's items, and a selection made by the same
  click does not survive the item being rebuilt. It does not by itself
  explain a click on "anything", nor (c), where nothing in the page is
  clicked: for those, what the page does when the SELECTION changes is the
  place to look (a selected dimension's label being positioned, or its
  references being repaired -- the log has 220 "no exact match for changed
  2d reference" lines from the dimensions).
Not said yet: whether (a) happens on an empty spot of the page or only on
items, and whether it happens in a page of a document with no errors.

**Corrected by the reporter, and an audit asked, 2026-10-07 15:26:** "not
everything then. it's just that a seemingly raondom click will trigger
recompute. we need to audit Techdraw for unnecessary recompute". So (a) is
not every click: now and then a click, with no pattern the reporter can see,
starts a recompute. And the request is wider than this page: go through
TechDraw for recomputes that are not needed.
**An inventory to start the audit from, by the note-taker, from the source
only (nothing run, nothing judged yet):** in `src/Mod/TechDraw/Gui` there are
117 calls of `Gui::Command::updateActive()` (a recompute of the document), 62
of `recomputeFeature()` (one object), 71 of `requestPaint()` and 6 `touch()`.
Most sit in commands and task panels, where the user has just changed
something: `CommandExtensionPack.cpp` 29, `CommandExtensionDims.cpp` 29,
`TaskDimension.cpp` 22, `TaskLeaderLine.cpp` 17, `CommandAnnotate.cpp` 16,
`Command.cpp` 15, `TaskCenterLine.cpp` 13, `TaskBalloon.cpp` 13, and less
elsewhere. The ones that a CLICK IN THE PAGE can reach are few, and are
where "seemingly random" would come from -- each runs at the end of a drag,
and the question for each is whether a press and release that moved nothing
is told from a drag:
- `QGIViewDimension::datumLabelDragFinished` (`QGIViewDimension.cpp` 700-714)
  -- writes the dimension label's `X`, `Y`, then `updateActive()`;
- `QGIViewBalloon.cpp` 505-518 -- a balloon's `X`, `Y`, and its origin if
  that was dragged, then `updateActive()`;
- `QGIHighlight.cpp` 78-94 -- a detail view's highlight: `AnchorPoint`, then
  `updateActive()`, from a timer;
- `QGISectionLine.cpp` 685 -- the section line (entry 21, fixed there);
- `QGILeaderLine::restoreState` (`QGILeaderLine.cpp` 306-314) -- a leader's
  points put back, then `recomputeFeature()`;
- `QGSPage.cpp` 580-596 -- a balloon being created;
- `QGIView.cpp` 191 -- a view dragged: `setPosition()`, which writes `X` and
  `Y`; no recompute of its own, but a written property marks the object;
- `MDIViewPage.cpp` 218, 224 -- after undo and redo;
- `ViewProviderPage.cpp` 252 and `ViewProviderViewPart.cpp` 374 --
  `recomputeFeature()` from a view provider.
What "unnecessary" would mean, for the audit to apply: a recompute after
nothing was changed (a click that moved nothing; a value written equal to
the one it had); a recompute of the DOCUMENT where one object changed (every
`updateActive()` above, against `recomputeFeature()`); and a recompute that
only repeats one that has just failed, which is what makes it noticed in
`scanner.FCStd`.

## 36. TechDraw drawn by the backend: dashed lines do not behave as Qt's -- OPEN

**2026-10-07 16:18 and 16:30.** First a question, "check if this is queued.
TechDraw backend renderer draws dash line zoom handling" -- it was not: the
only mention was a line under entry 20 and in
`docs/TechDrawPortAndSection.md`, as a known difference of the view FRAME
alone. Then the report: "most dashed line rendering does not behave the same
as Qt, namely view frame, section line, hidden line, and so on."
So, with the page drawn by the backend (`PageRendererVg`): dashed lines in
general -- the view frame, section lines, hidden lines, others -- do not
come out as the Qt page draws them, and how the dashes take a zoom is the
heart of it. Not said yet: what exactly differs for each kind (dash length,
gap, where the pattern starts, how it follows the zoom), and at which zoom.
Read from the documents and the source by the note-taker, nothing run:
- What is written down already, for the frame only
  (`docs/TechDrawPortAndSection.md`, "Still different from the Qt page"): "A
  frame's dashes grow with the zoom (the line stays a hairline): Qt counts a
  cosmetic pen's dashes in device pixels." So there are two kinds of pen in
  the Qt page and they take a zoom differently: a COSMETIC pen (the frame, a
  width in pixels) keeps its dashes the same size on screen at any zoom; a
  pen with a real width (hidden lines, centre lines, section lines) has its
  dashes in multiples of the line width, on the paper, so they grow and
  shrink with the page. The backend has to tell the two apart to match.
- Where the patterns come from: `LineGenerator::getLinePen`
  (`src/Mod/TechDraw/App/LineGenerator.cpp`) builds a `QPen` with a custom
  dash pattern out of the line standard's files
  (`src/Mod/TechDraw/LineGroup/*.LineDef.csv`, element lengths in pen widths
  in `*.ElementDef.csv`), with a dash OFFSET for a pattern that starts on a
  gap, and the cap style from the preferences -- "if the cap style is Round
  or Square, the lengths ... will be wrong by 1 pen width". Offset, caps and
  the proportional or absolute lengths of the ANSI file are three more things
  a second renderer can take differently.

## 37. TechDraw: the edge style "Chain" is not drawn dashed, by either renderer -- OPEN

**2026-10-07 16:30, a defect, noted with entry 36.** "BTW, one edge style
'Chain' does not render as dashed in both renderer, even though it shows as
such in the style combobox". An edge given the style "Chain" is drawn as a
plain line by the Qt page AND by the backend, while the style combo box shows
a dashed sample for it. Being in both, it is not the backend's; it is in what
both are fed.
Read from the source by the note-taker, nothing run:
- "Chain" is line 17, the LAST, of the ASME standard's list
  (`src/Mod/TechDraw/LineGroup/ASME.Y14.2.2008.LineDef.csv`:
  `17,Chain,LongDash,Space,Dash,Space`). The same pattern as line 4,
  "Center", and 5, "Symmetry", which the reporter did not name as failing.
- `LineGenerator::getLinePen` gives a plain solid pen for a line number
  below 2 or ABOVE the number of definitions loaded, and takes definition
  `number - 1`. The last line of a list is the one that falls off first if
  the count and the number disagree by one -- one definition not loaded, or
  the number kept as a place in the combo box rather than as the line's
  number.
- Entry 19 found and fixed that very shape in the PREFERENCES ("count >
  number" left the last style of each list unselectable, and the next Apply
  stored the first in its place; fixed `805b5afb25`, staged 14:23). The edge
  style of a view's line -- the combo box the reporter means -- is another
  path and was not part of it.
Not said yet: which line standard is selected (Chain exists in the ASME list
only), and where the style was set (the line decoration panel, a cosmetic
line, a centre line).

## 38. Omni search: an obvious freeze the first time it is brought up -- FIXED `bb31f8820b`, not staged (see `docs/HandsOnLog.md`)

**2026-10-07, said by the reporter to the build session directly** and
passed on by it at 16:35 to be numbered here: "do entry 23 next. while doing
it optimize omni search first bring up speed. right now there is an obvious
freeze time". The second sentence is this entry: the first time the omni
search is brought up in a session the program visibly freezes before the box
appears.
What the build session found and did, in its words (its log has it under
"Omni search: the first bring-up freezes"): the first bring-up loaded and
rendered the icon of every command, 609 of them, before showing the box --
0.99 s + 0.28 s on the reporter's configuration with `scanner.FCStd` open,
0.15 s + 0.07 s now.
The first sentence, "do entry 23 next", the build session could not place --
entry 23 is the settings audit, staged, its defaults fixed since -- and is
asking the reporter itself whether entry 24 is meant (every setting behind a
generated class so the omni search finds it) or what is left under 23.

## 39. MSAA has not reached any view since 2026-09-07 -- FIXED `c7d115e576`, not staged (see `docs/HandsOnLog.md`)

**2026-10-07, found by the build session** while answering the reporter on
entry 26, and passed on at 17:05 to be numbered here (its log has it under
"MSAA does not reach the view"). The reporter's words to it, on the halt of
entry 26: "what is the problem is entry 23. have you tested with scanner open
and then change the msaa setting".
What it found, in its words: with "MSAA 4x" chosen the backend prints "4x
MSAA scene targets could not be created on this backend -- rebuilding
without multisampling", and the session draws without multisampling from
then on. Cause: `fd5a9a5aa6` built the scene depth readable as a texture at
every sample count, and bgfx refuses a framebuffer with such a multisampled
depth. Every backend, not Direct3D alone.
The fix: the depth is write-only under MSAA; a test asks the view how many
samples it was built with (11 PASS, 5 FAIL on the staged binaries). The
reporter's own case on the fixed tree -- their configuration, `scanner.FCStd`
open, anti-aliasing changed in the dialog, OK -- 0.75 s in all, both views
rebuilt at 4 samples.
What it means for two other entries: the reporter's "regardless whether msaa
is used or not" of entry 25 was said while MSAA was not in effect at all, and
the toggling of entry 26 was toggling a setting that reached no view.

## Inbox

Notes not sorted into an entry yet. Add a line here at any time, in any words;
it is read before each entry is started and moved up into the table.

(empty: the notes of 2026-10-06 are entries 20 to 28, those of 2026-10-07 so far
entries 29 to 39)
