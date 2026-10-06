# Hands-on queue

Problems found by running the program by hand, in the order they were
reported, worked in turn. One entry per problem: what was seen (the reporter's
words where there are any), what it turned out to be, and the commit that
closed it. The copy under test is the staged one -- `docs/DevEnvironment.md`,
"A second env for hands-on testing" -- so a fix reaches the reporter only at
the next stage, and each entry says which stage has it.

States: `OPEN` (not looked at), `FOUND` (cause known, no fix yet), `FIXED`
(committed and tested in the dev tree, not staged yet), `STAGED` (in the copy
under test, waiting for the reporter to confirm), `CLOSED`.

Evidence that does not belong in the repository -- configuration snapshots,
report views, the reporter's own files -- is kept beside the dev tree under
`..\dl\handson\<date>\`, and an entry names what it holds.

| # | Reported | Problem | State |
|---|---|---|---|
| 1 | 2026-10-06 | idle progress bar in the status bar | FIXED |
| 2 | 2026-10-06 | `scanner.FCStd` opens to errors and an empty document | OPEN |
| 3 | 2026-10-06 | navigation style tooltips in the status bar are clipped | OPEN |
| 4 | 2026-10-06 | status bar dimension reads `100 mm x 80 mm`, wanted `100 x 80 mm` | OPEN |

## 1. Idle progress bar in the status bar -- FIXED

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

## 2. `scanner.FCStd` opens to errors and an empty document -- OPEN

**Reported (2026-10-06):** "with current setting (take a snapshot of user.cfg)
opening d:\Zheng.Lei\mech\scanner.FCStd got a bunch of warning and errors in
console without showing anything in the tree or the 3d view."

**Evidence:** `..\dl\handson\2026-10-06\` -- `user.cfg.ondisk` and
`system.cfg.ondisk` (the files as they were on disk at 09:37),
`user-BaseApp.live.cfg` (the running program's parameters, exported),
`report-view.log` (the last 400 report view lines).

**Seen in the running copy:** the document `scanner` is open with 0 objects.
The report view is one message repeated per object of the file: `Cannot create
object 'Dimension097': (The document 'scanner' is still being filled in, and a
command may not change it until that finishes. ...)`.

## 3. Navigation style tooltips in the status bar are clipped -- OPEN

**Reported (2026-10-06):** "the tooltips of navigation style option in status
bar is clipped."

## 4. Status bar dimension: `100 x 80 mm` -- OPEN

**Reported (2026-10-06):** "in the status bar the dimension, instead of
something like 100 mm x 80 mm, write it as 100 x 80 mm."
