# Hands-on log

The work side of `docs/HandsOnQueue.md`. The queue holds what was asked, in
the reporter's words, and is written by the note-taking session alone; this
file holds what was done about it, and is written by the build-and-test
session alone. Two files so that two sessions never write the same one.
Entry numbers are the queue's.

Per entry: the state, what it turned out to be, the commit that closed it,
how it was scored, and what is still open. Work up to the stage of
2026-10-07 10:37 is written into the queue's own entries, where it was put
before the split; this file starts after it.

States, as in the queue: `OPEN` (not looked at), `FOUND` (cause known, no
fix yet), `FIXED` (committed and tested in the dev tree, not staged),
`STAGED` (in the copy under test, waiting for the reporter), `CLOSED`.

Stages since the split: 2026-10-07 14:23 (`1c8781a7e1`, the code of
`c7a27b5a85`: entries 15, 17, 19, 22, 23 and 27), on the reporter's word
("stage it"). Smoke-tested on the staged copy right after: the omni search,
view cell menu and line style GUI tests, 41 PASS of 41, and the ring of
entry 15 valid after the refine.

The tree at the pause of 2026-10-07 (`c7a27b5a85` and the log after it): ctest 782 of 782 passing, `DeferredLoad_tests_run` left out (its known
timeout, `docs/Testing.md`). Staged 14:23; not pushed.

Pushed 2026-10-08 late, on the reporter's word ("Push"): origin/PartDesignPort at
`8a7e02b37c`, which has entries 41, 42, 44 and 45. Not staged.

Evidence that does not belong in the repository is under
`..\dl\handson\<date>\`, as before.

| # | State | In one line |
|---|---|---|
| 15 | STAGED `1047cc0647`; one question for the reporter | a refine wrote into the feature underneath. Left: `Pocket040` is 12 mm where the file has 13 -- its negative `Fit` grew in the old build, on one oddly made face |
| 17 | STAGED `3c8cd63032`; what it uncovers is a question for the reporter | the two sketches refer to edges of a binder that moved with another binder; found again now. Then `Pad033` loses its profile, because the sketch really changes |
| 19 | STAGED `805b5afb25` | every place the audit listed that runs at load, recompute or paint, the three wrong results, and the writer of -1; what is left is listed |
| 22 | STAGED `5aedd5cf83` | `/word` is an object query; the beginning of a keyword lists modes and objects |
| 23 | STAGED `c7a27b5a85`, and `08b8f009aa`; the defaults FIXED `02cab053df`, not staged | 574 settings: 221 had no documentation, 94 ran past 400 characters; all have a short text now, and a test keeps it so. The defaults: OK on a fresh profile changed 23 settings and stored 2 under a wrong type -- 14 of them a spin box clamping its default to 99, which the reporter's own profile carries |
| 24 | C++ SIDE DONE, about 1180 settings listed; the reporter's decisions of 2026-10-08 applied in two rounds, `e21eff05a7` and `427ffc8d28`; pushed `b70cc6ebf1`, not staged | every setting C++ reads is behind a generated class. After both rounds: the editors' font is Courier, Home is Top, the marker size is 7 everywhere, the Asymptote height is empty, CAM's unit default is upstream's; 15 of the 16 findings are fixed or dropped as decided (D4 needs nothing). A24: the fork's three accent colours stay ("keep ours"). Nothing is left with the reporter. Entries 41 (Python door) and 42 (state keys, to be listed) are decided and not started |
| 26 | FIXED `175ffce199`, not staged | the first OK of a profile held the program 11 to 15 s on the reporter's configuration with `scanner.FCStd` open: 780 keys stored for the first time and taken for changes -- stylesheet set again 4.2 s, every Part view provider re-meshed 3.2 s, language activated again 2 s. 0.9 s now |
| 27 | STAGED `fa2ada985c` | the cell menu made a spreadsheet view by asking for it, listed a page's views, and a pick was placed by the general policy |
| 31 | FIXED `47b5e72c79`, not staged | "Go to end" is on for a profile that never stored it |
| 32 | FIXED `b960092ea5`, not staged | the menus styled see-through are single objects shared between a pop-up over the 3D view and the main menu; the blue is the palette's bright text, the desktop's accent. No sheet chosen is an ordinary menu now |
| 33 | FIXED `ef4df215b5` (cycles `35a3bd898`), not staged | the path tracer's CUDA probe ran `cmd.exe /c where nvcc` through `popen` at the first 3D view; it searches the PATH without a shell now |
| 34 | FIXED `3d7b4c30fd`, not staged | Dark and Light store TechDraw's preselection colour, the blue of the 3D view's |
| 38 | FIXED `bb31f8820b`, not staged | the omni search's first bring-up made the icon of every command before showing the box: 1.27 s on the reporter's configuration, 0.22 s now |
| 39 | FIXED `c7d115e576`, not staged | MSAA has not reached any view since 2026-09-07: the scene depth was built readable at every sample count and bgfx refuses that framebuffer. Write-only under MSAA now; a test asks the view its sample count |
| 41 | FIXED, all four steps: `a75b43f1d5`, `4a99a978f7`, `48037fbd8c`, `de7bd49797`, `ff12279ee6`, then `aa63b07cc8` and `6a2216d0f0` for the reporter's answers to the list; nothing left with the reporter; pushed 2026-10-08, not staged | the Python-only modules' settings through a door into the registry: 603 settings listed that were not -- Assembly 13, Draft and BIM 426, Fem 47, CAM 27, the Addon Manager 41, Help 14, OpenSCAD 15, ReverseEngineering 11, Tux 5, Material 4. Registration only, the readers keep their code; a test per module holds each described default to its readers' |
| 42 | FIXED `aa3e77137c`, `dbadedb7ba`, `7e442e1bc2`, `234572bd87`; pushed 2026-10-08, not staged; Q6 not answered | of the about 300 keys C++ read without a definition, 174 are defined now -- 89 settings and 85 state keys, 213 rows of the registry: the 3D mouse and the expression sandbox with every reader converted, Gui's small groups too, and the state the program keeps defined with its readers left as they are. What stayed out is listed |
| 44 | FIXED `813d0250f9`, pushed 2026-10-08, not staged | the C++ DXF exporter was pointed at `Mod/Import` for its options, where nothing stores them; it takes them from `Mod/Draft`, where the DXF page puts them, as upstream does. An ellipse with "as polylines" on was an ELLIPSE before, an LWPOLYLINE after |
| 45 | FIXED `c7fdcf3220`, pushed 2026-10-08, not staged | a spreadsheet's view provider made its view when it was only asked whether it had one: one click on a sheet in the tree opened it. Asking is a question now, and a new request opens the view for the three callers that host it. Show-in-cell also took a stale cell and closed another sheet's view; it takes the active view's cell |

**The reporter, 2026-10-07 14:20, on what is open** (said to the build
session; the queue has the reporter's own entries):
- entries 15 and 17: "skip entry 15 and 17 for now" -- their open questions
  rest;
- entry 23: "yes fixed the defaults, leave the unused ones" -- the defaults
  that disagree between a definition and its preference page are to be made
  to agree; the settings nothing reads stay. Done, `02cab053df`;
- entry 26: "probably not the view provider, because the delay I experience
  is longer. most likely related to stylesheet re-apply" -- so the second
  measured on `scanner.FCStd` is not the halt; what OK does to the
  stylesheets is where to look (entry 30's freeze is the same family).
  Looked at, and it is the largest part of the halt: `175ffce199`.

## 15. A Pad "up to first" gives a third result -- FIXED, one question left

**It was neither the pad nor the pocket.** The queue's entry had it down to
`Pocket017` coming out wrong only in a recompute of everything, with inputs
that measured the same. Caught inside that recompute (a document observer,
`pad15g.py`): `Pocket017` comes out of its OWN recompute right -- 352.2463,
valid -- and is wrong ten objects later. Looked at after every object
(`pad15h.py`): it goes wrong the moment `Helix002` has been recomputed, the
subtractive helix built on it. One feature's recompute changed the shape of
the feature underneath, which nobody had touched.

**Which step.** `Helix002` has `Refine` on. With `Refine` off the pocket
stays right; `FixShape` makes no difference either way (`pad15j.py`). By
hand: the pocket cut by the helix's tool leaves the pocket alone, `fix()` of
the result leaves it alone, `removeSplitter()` of the result breaks it.

**Why.** `Pocket017` cuts a slot through a ring of radius 7, 0.7 high. The
half of the ring that crosses the cylinder's seam is left as two faces of
0.23 turns each, one either side of the seam. The helix's cut shares those
two faces, and their edges, with the pocket's shape -- a boolean keeps what
it does not cut. The refine joins the two into one face of 0.47 turns
(`FaceTypedCylinder::buildFace`, `src/Mod/Part/App/modelRefine.cpp`), and
it built that face over the old faces' own surface, out of their own edges.
An edge keeps one pcurve per surface, so the new face found the pcurves the
old faces use; `ShapeFix_Face` then moved those of one side by a period, to
make the new face continuous across the seam -- in place. The pocket's old
face on that side now had two edges a full turn away from its other two:
`u` 0 to 7.7466 for 0 to 1.4634, 1.23 turns for 0.23, "unorientable" to the
kernel's check, and a volume of 444.86 for 352.25. The same on the ring of
radius 5.75 above it.

That explains everything the queue's entry lists: the copies that never
showed it (a copy gives every face a surface of its own, so nothing is
shared); the BREP files that never showed it; the pocket alone being right
(nothing refines on top of it); the same wrong number in the 7.7.2 build
(this code is years old, and upstream has it too); and the eight invalid
shapes, which are `Pocket017`, what is built from it, and the same thing
again under `Pad051`.

**Without the document:** a ring cut through by a slot, a second cut
somewhere else, `removeSplitter()` on the second -- and the FIRST is no
longer valid (`syn15.py`; 347.78 -> 419.62, one face of 1.23 turns).

**Fix,** `1047cc0647`. The face that replaces others gets a copy of the
surface, and each of its boundary edges a copy of the pcurve it had on the
old one. `ShapeFix` sees what it saw before and moves only what is the new
face's own; an edge that had no pcurve there gets one from `ShapeFix`, as
before. For the cylinder and the B-spline builders. The plane builder is
left as it was: a plane has no period to move a pcurve by.

**Scored.**

| | before | after |
|---|---|---|
| the ring, the first shape after the second is refined | not valid, 419.62 | valid, 347.78 |
| `Pocket017` after `Helix002` is recomputed | not valid, 444.86 | valid, 352.2463 |
| `scanner.FCStd`, recompute of everything: shapes not valid | 8 | 0 |
| `Pad051` after it (file: 3256.8230) | 3282.67 | 3256.8230 |
| `Pocket039`, `Boolean003`, `Pocket037` | not valid | valid, as in the file |

`parttests/regression_tests.py`, `test_refine_leaves_its_input_alone` (fails
before, passes after). TestPartApp 275 OK. TestPartDesignApp 346, 2
failures: the two `TestThickness` 5829 cases, which fail the same way on
the staged binaries that do not have this change (`docs/Testing.md` has
them). Evidence: `..\dl\handson\2026-10-07\entry15-*`, the probes
`pad15g.py` to `pad15n.py` and `syn15.py` beside them.

Seen on the way and left: `TopoShape::fix()` runs `ShapeFix_Shape` on the
original shape once a trial on a copy has succeeded, on purpose (a copy
flattens instances). That is the same kind of risk -- it writes into edges
the shape shares with others. It did not change the pocket in this case
(measured), so it was left alone.

**What is left: `Pocket040` to `042` -- a question, not a defect.** After a
recompute of everything three volumes still differ from the file:
`Pocket040` 2703.02 for 2468.59, and `041`, `042` after it by the same
234.43. (`PolarPattern003` differs in the fifth digit, 38.0108 for 38.0347;
not looked at.) Three objects are in error, all known: `Sketch043` and
`Sketch055` (entry 17), `Fillet011`.

`Pocket040`'s profile is a face, `Hole007.Face56`, a disc of radius 12.5,
with `Fit` -0.5. The file's tool has radius 13; recomputed here it has 12.
With `Fit` +0.5 this build gives the file's pocket to the last digit, so the
two lengths of the old `TwoLengths` are read right and only the direction of
the fit differs. Measured with `fit1.py` to `fit7.py`, each run in the build
that wrote the file and in this one:

- The rule is the same in both builds: a negative `Fit` shrinks. A pad's
  top and bottom faces, a sketch, the bottom of a pocket, the bottom of a
  counterbored hole, for a pad and for a pocket: all shrink, in both.
- The file's `Face56` is the exception, and it is the old build's own. As
  saved it is a reversed face on a plane whose axis points the other way
  (-z), and the kernel's 2D offset of a lone circle takes its sense from the
  plane it finds on the edge: `makeOffset2D(-0.5)` GROWS that face, in both
  builds. So the old build's pocket was 13 -- a negative fit that grew.
- This build recomputes `Hole007` on opening (the migration recompute), and
  its `Face56` comes out a forward face on a +z plane. On that the same
  call shrinks, and the pocket is 12.

Nothing in the pocket writes into anything: an earlier reading here, that
the pocket's recompute changed the shared edge, was wrong -- a recompute "of
the pocket" recomputes the 26 touched features under it first, `Hole007`
among them, and the face asked afterwards was the new one. A probe in the
pocket's code (temporary, removed) showed the edge it offsets has no plane
of the old kind on it.

So this build does what the property says and the file was built on the
exception. No code was changed for it. For the reporter to say: keep 12, or
set `Pocket040.Fit` to +0.5 in the file to have the 13 it was drawn with.
Making the offset's sense independent of the plane an edge carries would
not bring 13 back either; it was not done, since no face this build makes
was found to show the exception.

**Answered (the reporter, 2026-10-07 13:20, through the queue):** set
`Pocket040.Fit` to +0.5 in the file. The file is the reporter's; nothing
here edits it.

## 17. `Sketch043`, `Sketch055`: "Missing external geometry reference" -- FIXED

**What they refer to.** Both sketches take external geometry from
`Binder017`: `Sketch043` its `Edge1`, `Sketch055` `Edge1` and `Edge2`.
`Binder017` binds `Face3` of `Binder008`, the Mutated binder of entry 16.
When `Binder008` is recomputed it moves 53 mm (the file's own state, entry
16) and its elements get other names; `Binder017` is rebuilt from the moved
face, so it moves by the same 53 mm and its four edges are renamed too
(`;#888f;:H58b,E` is what the sketches hold; the string behind `#888f` no
longer exists).

**Why the repair missed.** A reference that loses its name is searched by
its geometry in the generations a feature keeps of its shape
(`Part::Feature::searchElementCache`). Since entry 16 a binder whose
container moved says by what, and its generations are searched moved the
same way -- that is what found `Binder008`'s faces again. `Binder017` has no
container that moved: seen from itself nothing happened, its support just
came back somewhere else. It reported no motion, and its edges were looked
for where they had been. The one face it has was found only because a shape
with a single face of the kind leaves no choice; four edges do.

**Fix,** `3c8cd63032`. When an element is not found where it was and the
feature named no motion, the live shape is compared with the generation as
a whole: `Part::recoverShapeMotion` says whether it is the same shape, index
for index, carried off by a rigid motion, and by which. If so the element is
searched moved by it. Asked once per generation and live shape, and only
after the plain search has failed, so a shape that did not move pays one
comparison of vertex counts at most.

**Scored** on `scanner.FCStd`, recompute of everything (`e17a.py`):

| | before | after |
|---|---|---|
| `Sketch043` | "Missing external geometry reference", `Binder017.Edge1` dropped | up to date, `Binder017.Edge1` kept |
| `Sketch055` | the same, both references dropped | up to date, both kept |
| objects in error | `Sketch043`, `Sketch055`, `Fillet011` | `Pad033`, `Fillet011` |

`TestShapeBinder.testReferenceIntoBinderOfMovedBinder` (a third binder and
a sketch's external geometry on a binder of a moved binder): fails on the
staged binaries ("Failed to obtain shape ...RefFace3.?Edge3"), passes here.
TestShapeBinder 9 OK, TestPartApp 275 OK, TestSketcherApp 145 OK,
TestPartDesignApp 346 with the two known `TestThickness` 5829 failures (run
before the new case was in the build tree; its module was run after), the
Part and Sketcher cases of ctest 24 of 24. Evidence:
`..\dl\handson\2026-10-07\entry17-*`.

**What it uncovers: `Pad033`, and it is the model.** Before, the two
sketches failed and everything on them kept the shape the file had. Now
they recompute, and `Sketch043` comes out different: the edge it takes from
`Binder017` lies at x = -10.508 in the sketch where the file had it at
-7.272, because the binder is 53 mm along the group's own z and 3.24 mm
sideways from where it was. The line constrained onto it moves with it, one
arc collapses to a point, the outline no longer closes, and the sketch has
five regions where it had six. `Pad033` pads `InternalFace2`, a region of
212.09 mm2 that is not there any more: "Sub shape not found:
Sketch043.?InternalFace2" (`e17b.py`). No name was lost; the geometry is
gone.

So after entries 8 and 14 to 17 the migration recompute of this file ends
with two errors, and both are consequences of `Binder008` being brought up
to date after its group was moved -- which the build that wrote the file
never did, since it had no reason to recompute the binder. `Fillet011`
("Invalid edge link") was not looked at separately. Whether a Relative
binder in a moved group is MEANT to come back 53 mm away is the reporter's
to say; if it is, the file needs its sketch redone, and if it is not, the
thing to change is the binder, not the references.

## 19. TechDraw: other indexes taken on trust -- FIXED

The queue's entry is the audit, read from the source and not run. What was
done with each of its points, `805b5afb25`:

**Enumerations a file holds outside their list** (the audit's 1, 3, 4 and
6: `Type` of a projection group item and of a dimension, `BubbleShape`,
about twenty callers of `getValueAsString()`). Not fixed caller by caller.
Every TechDraw enumeration has a fixed list set in its constructor, so a
`DrawView` and a `DrawPage` now note what their enumerations hold before a
restore and put back any the file left outside its list
(`RestoredEnumerations`, `DrawUtil`), with a warning that names the
property. One place, and a caller added later is covered too.
`TDTest/RestoredEnumerationTest.py` writes 7, 99, 99 and -3 into a saved
file for a page's `ProjectionType`, a view's `ScaleType` and a balloon's
`BubbleShape` and `EndType`, and reads back what a new object holds; on the
staged binaries the first of them reads `None`.

**Lists indexed by a reference number.** `DrawViewDimension`: the saved
geometry, and the references, are asked for their size first in the three
places that did not (2). `LandmarkDimension`: a file with fewer tags than
references gets the missing reference vertices instead of an exception (7).

**The line definition files** (8). A row too short, or a length that is
not a number, no longer ends the `LineGenerator` constructor in an
exception; a short row still counts, since rows are looked up by place.

**`DrawProjGroup`, the two lines that disagreed by one** (5). The audit had
the second one right: `arrangeViewPointers` read the projection angle
preference one entry too far, so a group with no page was laid out Third
Angle for First, and threw for Third. Both read the same entry now.

**The three wrong results.**
- `LineGenerator::fromQtStyle` asks for the standard body's name. It
  compared the preference, a place in the sorted list of files (ANSI, ASME,
  ISO), with an enum in the order ANSI, ISO, ASME.
- `Preferences::HighlightLineStyle` reads `LineStyleHighlight`, the key the
  page writes. A highlight line style chosen in the preferences now takes;
  it never did.
- The annotation page selects a line style when its number is within the
  list. "count > number" left the last style of each list unselectable,
  and the next Apply stored the first in its place.

**The writers of -1.** `Gui::PrefComboBox` with no current item stores
nothing. It stored -1, in every module, for whatever read the key as an
index. `tests/gui/techdraw-line-style-prefs.py`: 16 PASS, 4 on the staged
binaries (the last style not selected, the first stored over it, -1 stored
for an empty selection, for each of the four boxes).

TestTechDrawApp 7 OK; `techdraw-line-standard-out-of-range.py` 5 PASS
still. Evidence: `..\dl\handson\2026-10-07\entry19-*`.

**Left as the audit listed them, not changed:**
- the task panels that set a document property from a combo box's
  `currentIndex()` (TaskLeaderLine, TaskBalloon, TaskDimension,
  TaskRichAnno): their lists are filled when the panel is made, so there is
  a current item; and a -1 that did get into a file is repaired at the next
  open now;
- the dialog-only places (`TaskProjGroup`, `TaskSectionView`,
  `TaskComplexSection`, the line group row with fewer than four fields);
- what the audit did not cover: the fixed `references.at(1)` and `.at(2)`
  of the dimension helpers, the restore of cosmetics and centre lines,
  broken and complex sections, details, templates, weld symbols, hatch and
  PAT parsing, the command files and the Python;
- `DrawTemplate`'s one enumeration, which nothing asks for its text.

## 22. Omni search: `/word` with no space is an object query -- FIXED

`5aedd5cf83`. The rule as the queue has it decided:

| typed | is |
|---|---|
| `/` | the chooser, three modes |
| `/Box.Length` | an object query, as `/ Box.Length` |
| `/ cmd` | an object query for something called `cmd` |
| `/cmd`, `/param` | the keyword: the chooser shows that one mode |
| `/cmd `, `/param ` | the mode, as before |
| `/c`, `/par` | the chooser: the modes it could be, then the objects it matches |

`OmniSearch::parseInput` (the grammar, `Input::withObjects`), and
`OmniSearchEdit::fillChooser` (the list). The chooser's rows are made for the
text as it stands instead of filtered from a fixed three. Its object rows
come from a second expression completer asked through `completionsFor()`,
which shows nothing; they are kept to what the box can resolve, because that
completer offers units and functions as well (`/c` listed `cm`, `cd`, `acos(`
on the first try). Picking an object row picks the object, as a row of the
object list does. The browser viewer's `parseInput` (`web/src/omni.tsx`)
follows; its only keyword is `cmd`. `docs/OmniSearch.md` sec 1.

One thing seen on the way and kept as it was: an unfiltered completer spends
the first Down key on selecting the row that is already current. The chooser
became unfiltered with this change, so its first row is selected when the
list comes up; the command and parameter lists have always behaved the other
way and were left alone.

Scored: `OmniSearch_Tests_run` `test_parseInput` (the table above);
`tests/gui/omni-search-slash-word.py` 10 PASS, 7 on the staged binaries (no
Crate row for `/c`, nothing to pick, `/Pillar.Height` resolves nothing). The
web side type-checks (`tsc --noEmit`, emsdk's node); the bundle is built with
the WASM viewer and was NOT rebuilt.

"Also check wasm viewer, I remember it already shows `<space>` there": its
two mode rows are titled `/ ` and `/cmd ` with a real trailing space, which
is not visible; there is no literal `<space>` anywhere in `web/src`. Left as
it is.

## 23. Omni search: the settings it collects -- FIXED

**The audit.** The omni search lists what the generated parameter classes
register: 574 settings in 13 classes (`audit23.py`, which imports the
definition files as they are). The row shows the title and the first line of
the documentation, the tool tip all of it.

| | settings | no documentation | over 400 characters |
|---|---|---|---|
| RenderParams | 157 | 6 | 78 (longest 2248) |
| ViewParams | 189 | 60 | 6 |
| PartParams (App and Gui) | 54 | 48 | 3 |
| DocumentParams | 43 | 32 | 6 |
| TreeParams | 36 | 30 | 0 |
| MeshParams | 18 | 18 | 0 |
| ReportViewParams | 11 | 7 | 1 |
| SheetParams | 10 | 10 | 0 |
| Expr, Overlay, Link, Group, OpenView | 56 | 10 | 0 |
| all | 574 | 221 | 94 |

The long ones are what the entry suspected: the design note of the change
that added the setting, used as its documentation -- measurements, history,
"RE-MEASURED 2026-08-15", references to documents.

**Done,** `c7a27b5a85`.
- The 94 say what the setting does, its values and when it applies, in at
  most 400 characters (mean 215). The long form is kept, as a comment above
  the setting in its definition file.
- The 221 have a text, each written from the code that reads the setting
  (three read-only helper agents did the reading; every text was read back,
  and what they could not verify is in their notes beside the evidence).
- 18 settings whose generated title was broken have one of their own:
  "Force XM L", "Respect System DP I", "Axis XColor", the five `pref...` and
  the six `check...`.
- The generated `.h`/`.cpp` of the twelve classes are regenerated from the
  definitions, nothing by hand; line endings put back to what each file had.
- `ParamRegistryTest.everySettingIsDocumentedBriefly`, and the same check in
  `OmniSearch_Tests_run` with the Gui classes registered, fail on a setting
  with no documentation or with more than 400 characters. (The Gui one
  caught a 401st byte my own count had missed: a section sign is two.)
  Part, Mesh and Spreadsheet register only when their modules load, which
  neither test does; their settings are covered by the audit script, not by
  a test.

Evidence: `..\dl\handson\2026-10-07\entry23-*` (the audit before and after,
the three lists of new texts with their "unverified" notes).

**What the audit turned up besides.** One fixed, the rest listed for the
reporter; none of it was in what was asked.
- FIXED `08b8f009aa`: `prefLicenseUrl` was used only when it was EMPTY
  (`App/Document.cpp`), so a new document never got the address from the
  preferences.
- FIXED with the texts: `LinkParams.CreateInContainer` was defined
  `ParamBool('CreateInContainer', bool, False)`, the type in the default's
  place. The generated default was `true` and still is.
- Read by nothing: `ViewParams` `ShadowLightDirectionX/Y/Z`,
  `ShadowExtraRedraw`, `TransformOnTop`; `ViewSelectionExtendFactor` (its one
  use is inside `#if 0`); `MeshParams.DisplayAliasFormatString` (a copy of
  the spreadsheet's). Their text says "currently has no effect" where that
  is so. `ShadowLightColor` and `ShadowLightIntensity` reach no renderer:
  the page that syncs them writes view properties that were renamed to
  `Render_Light*`.
- Defaults that disagree between a definition and its preference page:
  `UseFCBakExtension` (False; the page and a second reader say true),
  `SaveThumbnail` (False; checked), `ThumbnailSize` (128; 256),
  `CompressionLevel` (3; 7, beside a label saying "3 = default"),
  `AutoValidateShape` (False; checked -- and OK on that page writes True),
  `checkShowReportViewOnWarning` (True; unchecked -- and saving writes
  False), `TreeEditColor`.
- The draw style page writes `HiddenLine_Color` and `HiddenLine_Width`; the
  view's properties are `HiddenLine_LineColor` and `HiddenLine_LineWidth`,
  so a change of those two never reaches an open view.
- `NoPartialLoading`: the page's tool tip says the opposite of its label.
- `ParallelRunThreshold` is no threshold on this kernel: any value above 0
  turns parallel booleans on.
- `Part` `MeshDeviation` and `MeshAngularDeflection`, the App copies: read
  only as the fallback of `TopoShape::meshShape`, which takes the percentage
  as an absolute deflection and the degrees with no conversion to radians.
- `DefaultDatumColor` (Mod/Part) colours binders and extrusions; datums read
  a key of the same name under Mod/PartDesign.
- The documentation of `HiddenLineOverrideFaceColor` and
  `HiddenLineOverrideColor` talks about selection highlighting: a copy and
  paste, left as it was.

**The defaults that disagree -- FIXED `02cab053df`** (the reporter, 14:20:
"yes fixed the defaults, leave the unused ones"). Not staged.

What a disagreement does: OK in the preferences saves every page, so on a
profile that never stored a setting OK stores what the page SHOWS for it.
Where that is not the default the program uses while the key is unset, the
setting changes with nobody having changed it -- one behaviour until the
first OK, another after.

So it was checked end to end instead of by reading: a fresh profile, the
dialog opened, OK, every key OK stored compared with its definition
(`tests/gui/preferences-ok-keeps-defaults.py`, registered as
`GuiPreferencesOkKeepsDefaults_tests_run`). On the binaries staged 14:23:
**23 settings changed by OK and 2 stored as the wrong type**; the list above
had six of them. After: 0 and 0.

- **Fourteen were the page generator, not a default.** It set a spin box's
  value before its range, so a default outside Qt's own 0 to 99, or finer
  than two decimals, was clamped -- and stored: the overlay's delay, hint
  delay and animation duration 200 -> 99, its wheel delay 1000 -> 99, its
  four hint lengths and the pie menu's radius 100 -> 99, the pie menu's delay
  200 and duration 250 -> 99, `DatumScale` and `ShadowGroundTextureSize`
  100 -> 99.99, `ShadowEpsilon` 1e-05 -> 0. `CyclesSamples` 256 -> 99 had no
  range at all. Fixed in `src/Tools/params_utils.py`: the value is set again
  after the range, and a numeric setting with no range of its own gets the
  range of its type (a stored `GpuMemoryBudgetMB` of 4096 would have been
  cut to 99 the same way).
  **The reporter's own profile carries these**: the copy of it taken
  2026-10-07 00:13 has `DockOverlayDelay`, `DockOverlayHintDelay`,
  `DockOverlayAnimationDuration`, `DockOverlayWheelDelay`, `PieMenuRadius`
  and `CyclesSamples` at 99. Nothing puts them back: a stored value is the
  user's as far as the program can tell. To get the defaults back, remove
  those keys or set them by hand (200, 200, 200, 1000, 100, 256).
- **The definition follows the page** where the definition predates an
  upstream change of the default that the page and the other readers took:
  `UseFCBakExtension` true, `SaveThumbnail` true, `ThumbnailSize` 256,
  `CompressionLevel` 7. The last was measured before it was chosen
  (`scanner.FCStd`, 686 objects, saved twice at each): level 3 2.5 s and
  6.42 MB, level 7 2.9 to 3.3 s and 5.55 MB. The page's label said
  "3 = default" beside a 7; it says 7.
- **The page follows the definition** where the definition is this fork's
  own decision: `AutoValidateShape` off (`7258b707a5`, "obvious performance
  impact on complex shape" -- the page still ticked it, and OK switched it
  on), `DefaultShapeColor` 0xCCCCE6 (`7f5a3b7d49`, the default material
  card; the page showed and stored 0xCCCCCC),
  `checkShowReportViewOnWarning` on, `AnnotationTextColor` white (the button
  had no colour and stored the palette's, 0xE3E3E3).
- `TreeEditColor`, in the list above, was not one: the Colors page sets its
  button from the setting when it is built. Nothing changed for it.
- **The Selection page's two highlight colours were spin boxes**, reading 99
  and stored as an INTEGER `HighlightColor`/`SelectionColor` that nothing
  reads (the colours are unsigned keys of the same names). They are off
  that page. Not replaced by colour buttons there, and that is a choice to
  confirm: the Colors page already has both, and of two pages storing one
  key the page saved LAST takes the other's change back -- a button on the
  Selection page would have looked right and done nothing.
- Left as they are, all three the same thing seen from the page's side:
  `MeshColor`/`LineColor` (0 means "the built-in colour", the page shows
  that colour and stores it), `prefLicenseUrl` (empty means "the chosen
  license's address", the page spells it out). The test lists them with the
  reason.
- Not a definition, so not in this: the Workbenches page rewrites its three
  lists on a first OK (`Workbenches/Ordered`, `Disabled`,
  `General/BackgroundAutoloadModules`), from empty to what it shows.

**Readers with a fallback of their own** (`defaults23.py`, every
hand-written `Get...("key", default)` against the definition of the same
key): `MarkerSize` is defined 9 and read with 7 by the Sketcher and the
mesh defect views, 5 by Robot, 4 by CAM; TechDraw reads the 3D view's
`SelectionColor`/`HighlightColor` with green and yellow of its own; BIM's
project manager reads `DefaultShapeColor`/`DefaultShapeLineColor` with
white and black; `AutoSaver.cpp` reads `SaveThumbnail` with false (its
toggling of the key does nothing here any more: the document's own
property decides). Listed, not changed: each is a module's own choice until
somebody says it is not, and only the first is a page-against-reader case
(a fresh profile draws sketch points at 7, and at 9 after an OK).

**The root, for a decision** (entry 24's ground): a page stores a key it
was only showing. If a preference widget stored only a value that was
changed, none of the above could happen, a first OK would not store 700
keys (entry 26), and two pages could show one setting. It would also stop
OK from "making the page's defaults real" where a reader has another
fallback, which some profiles depend on without knowing. Not done.

Evidence: `..\dl\handson\2026-10-07\entry23-ok-keeps-defaults-*`,
`entry23-defaults-scan-after.txt`, `entry23-compression-3-against-7.txt`,
`defaults23.py`, `pages23.py`.

## 26. A long halt after enabling MSAA and pressing OK -- FIXED

`175ffce199`. Not staged. The halt is reproduced, measured and gone; both of
the reporter's guesses were right, and neither was the whole of it. What was
found first is kept below as it was written.

**Reproduced.** A copy of the reporter's configuration as it was found on
2026-10-07 10:44 (Dark theme, nothing of the preferences ever confirmed:
`user-cfg-recovery\user.cfg.as-found-now`), `scanner.FCStd` open, the
preferences opened, the anti-aliasing changed, OK (`e26b.py`):

| | the click | event loop held after | |
|---|---|---|---|
| the first OK, staged 14:23 | 1.1 to 2.5 s | 9.9 to 12.7 s, one turn | 783 keys reported changed |
| the second | 0.4 s | 2.5 s | 70 |
| the third | 0.45 s | 0.84 s | 67 |
| **the first OK, after** | **0.49 s** | **0.42 s** | 779 |
| the second, after | 0.40 s | 0.14 s | 70 |

**Why the first.** OK saves every page. On a profile that never confirmed
the preferences about 780 keys are stored for the first time, and a key
being stored is reported as CHANGED to everything that watches it -- with
the value it had all along. (The reporter's "after first enabled it": it is
the first OK of the profile, not the anti-aliasing.)

**Where the time went** (the main thread's native stack sampled every 60 ms
through the 14 s, `sampler.py`, the dev tree for its symbols):

| | of the 14 s |
|---|---|
| the application's stylesheet set again (`applyStyleSheet` -> `Application::setStyleSheet`) | 4.2 s |
| ... of which the tree remaking the icon of each of its 686 items, because the palette is set twice on the way; most of it in file-attribute calls | 2.9 s |
| every Part view provider reloaded and re-meshed (`ViewProviderPartExt::reload`) | 3.2 s |
| the active language activated again (`applyLanguage`) | 1.0 s |
| the TechDraw preference page rebuilding its line style icons for that language change | 0.9 s |

So: the stylesheet, as the reporter said at 14:20; the view providers, as
the reporter said first; and the language, which nobody suspected.

**Fix.** Each handler acts on a difference, not on being told.
- `Application::setStyleSheet` compares what it is about to apply -- the
  sheet with its variables resolved, the icon set, the background, the
  palette as its last apply left it -- with what it applied, and returns when
  they are the same. A sheet edited on disk, an accent colour, a theme
  variable and a desktop that changed its colour scheme are all differences.
- Part's observer of the renderer `Type` and `RenderCache`, and the
  overlay's of the three keys its sheet is chosen by, compare values, read
  with the defaults of their definitions.
- The language handler leaves the active language alone.

**Scored.** `tests/gui/preferences-ok-reapplies-nothing.py`
(`GuiPreferencesOkReappliesNothing_tests_run`): 13 PASS; 3 FAIL on the
staged binaries. `part-tessellation-reload.py` 7 PASS (the observer still
acts on a real change), `theme-switch-contrast.py` 39 PASS,
`sketcher-preferences.py` 20 PASS.

**Left, and seen.**
- A first OK still stores its 780 keys and reports them changed; the
  handlers that are expensive no longer mind. The others were not looked at
  one by one: what is left of a first OK is 0.9 s. The root is the one named
  under entry 23, and it is entry 24's.
- After a theme SWITCH in the session the next apply is still a full one
  (the palette is still moving when the sheet is set). Once.
- A REAL stylesheet change costs what it cost: the tree's icons, 2.9 s on
  this document, with `QFileInfo` asked about each icon again. That is entry
  30's freeze from the other side, and worth its own look there.
- The reporter's profile had the clamped 99s of entry 23 from its first OK.

Evidence: `..\dl\handson\2026-10-07\entry26-*` (both probes' results before
and after, the two profiles, the single-write table), `e26b.py`, `sampler.py`.

**What was found first** (before the reporter's "probably not the view
provider", and true as far as it went). What was asked: is the halt the
update of every view provider that OK sets off, or the anti-aliasing change
itself?

**From the code.** OK saves every page, and a page saving a key writes it
whether it changed or not. `ParameterGrp::_SetAttribute` tells its typed
observers only of a changed value -- and then, "for backward compatibility",
notifies the old observers every time (`Notify(Name)`). Part's
`InstancingGateObserver` (`Mod/Part/Gui/PartParams.cpp`) is an old observer,
and on any notice named `RenderCache` or `Type` it starts the timer that
reloads every Part view provider of every document. So the reporter's guess
is right in kind: OK reloads all of them without anything having changed.

**Measured** (`e26.py`, the event loop held, `scanner.FCStd`, 686 objects, a
fresh configuration, Direct3D 11):

| step | event loop held |
|---|---|
| nothing written | 0.00 s |
| renderer `Type` written UNCHANGED | 1.07 s |
| `AntiAliasing` 0 -> 3 | 0.16 s |
| `AntiAliasing` 3 -> 0 | 0.15 s |

The anti-aliasing change is not the halt. An unchanged write of one key
costs a second on this document; what the dialog's OK costs in all -- every
page, every observer -- was not measured, and one second is not yet the
"long halt" reported.

## 27. The view cell menu -- FIXED

`fa2ada985c`. Three notes, one cause and a half.

**The cause.** To list what a cell can show, the menu asked every object's
view provider for its view (`getMDIView()`).
- `ViewProviderSheet::getMDIView()` answers by MAKING the view. So opening
  the menu opened a view for every spreadsheet of the document, placed by
  the general policy: into the last non-3D cell, closing what was there --
  the TechDraw page that "auto switched to spreadsheet" -- or, with no such
  cell, into a new split, the spreadsheet that was "auto created".
- A TechDraw view object answers with its PAGE's view, so every dimension,
  view and template of an open page was listed.
- And a pick: the page was opened by its view provider, placed by the same
  policy into the last non-3D cell -- the spreadsheet's, which it closed --
  and the menu's own "put it in this cell" then found it hosted already and
  did nothing. The choice was right and the cell wrong.

**Fix.**
- The open object views are read off the views, which carry their object's
  name as their own (`MDIViewPage` did; `SheetView` does now). Nothing is
  asked of a view provider to build the menu.
- Pages and spreadsheets are listed by type, open or not; a spreadsheet
  used to be listed only because listing it opened it.
- A view opened by a pick is opened FOR that cell
  (`ViewPlacement::IntoCell`); `Std_ViewCellShowObject` likewise. A view
  that sits in another cell is activated there, where the pick used to do
  nothing.
- `ViewProviderSheet::getMDIView()` still makes the view: selecting a sheet
  with "sync view" on relies on it, and changing that was not asked.

Scored: `tests/gui/view-cell-menu.py` 15 PASS. On the staged binaries the
menu of a lone 3D cell leaves a spreadsheet cell behind, and the menu of an
open page lists `Template`, `Front` and `Width`. `docs/SplitViews.md`
sec 5.5.

Not looked at: a spreadsheet's view closed from its cell ("Close view")
seems to stay alive hidden, and a double click on the sheet then shows
nothing -- seen once while writing the test, on the staged binaries, not
pinned down.

## 31. Report view: "Go to end" on by default -- FIXED

`47b5e72c79`. The option was a member of the report view initialised to
off, and the stored key only overrides it; the member starts on now
(`ReportOutput`, `src/Gui/ReportView.cpp`). A profile that stored
`checkGoToEnd` keeps what it stored, either way.

Scored: `tests/gui/report-go-to-end.py` (`GuiReportGoToEnd_tests_run`), 6
PASS. On the binaries staged 14:23, 300 lines printed into a fresh
profile's report view leave it at 0 of 4359: 1 FAIL, 5 PASS.

Not done, and entry 24's: the key is still read straight from the
parameter group, so the omni search does not list it.

## 32. Sub menus that are see-through with blue text -- FIXED

`b960092ea5`. Not staged.

**(a) Why.** `setupMenuStyle()` gives a menu the "menu style sheet" of the
Theme preferences, and with none chosen -- which is what every theme pack
sets -- it took `Dark.qss` or `Light.qss` of `Stylesheets/menu` whenever a
theme with a stylesheet was active; only a session with no stylesheet at all
got plain menus. It is called on purpose, on twelve menus: the 3D view's
pick menus (the ones remembered), the omni search's group menu, and four
action menus -- command history, select-up, the tool bar menu, the camera
binding. Each of those four is ONE menu object: shown over the 3D view by
its shortcut, and hanging in the main menu as a sub menu. So the style
meant for the pop-up over the view is what Tools > Command history showed.
Nothing leaks; the reporter's memory of what it was FOR is right.

The blue text is the sheet's `color: palette(bright-text)`. Under a dark
colour scheme on Windows that palette role is the desktop's accent:
`#a6d8ff` in the test session, against `#ffffff` for ordinary text.

**(b) Off by default.** With no menu sheet chosen a menu is an ordinary one,
drawn by the theme. The see-through sheets are for whoever picks one
(Preferences > Theme > "View menu style sheet", whose entry for none reads
"None" where it read "Auto"). A sheet taken away again makes the menus
opaque again in the same session; the pick menu's sub menus follow their
menu; the dark sheet's text is a fixed light grey instead of the accent.

Scored: `tests/gui/menu-see-through-is-a-choice.py`
(`GuiMenuSeeThroughIsAChoice_tests_run`), 7 PASS; 4 FAIL on the staged
binaries. `theme-switch-contrast.py` 39 PASS.

Not done: a profile that wants the old look has to choose the sheet; no
theme does it for them. Whether the 3D view's pick menus alone should stay
see-through by default was not what was asked ("disable transparent menu by
default"), so they are opaque too.

## 34. TechDraw's preselection colour and the themes -- FIXED

`3d7b4c30fd`. Not staged. As decided ("dark and light set techdraw"): both
packs store `Mod/TechDraw/Colors/PreSelectColor`, the blue (`0x0004FFFF`)
they give the 3D view's highlight. Classic keeps its yellow.

Scored: `tests/gui/theme-owns-techdraw-preselect.py`
(`GuiThemeOwnsTechDrawPreselect_tests_run`) -- Classic, Dark, Light, Classic
again, and what TechDraw would highlight with after each: 7 PASS; 4 FAIL on
the staged binaries (yellow after Dark and after Light). An open page
follows without being reopened: the colour is read at each hover
(`QGIView::getPreColor`). From the code, not run on a page.

One more way the key gets set, found under entry 26: OK in the preferences
stores it too, with whatever the TechDraw page shows. With every theme
owning the key that no longer matters for a theme switch.

The other packs in the tree (Dark behave, Dark contrast, Dark modern,
Darker, Light modern, ProDark) are not offered as themes and were left.

## 33. A cmd window at the first document opened -- FIXED

`ef4df215b5` (the submodule `src/3rdParty/cycles` moved to its `35a3bd898`).
Not staged. **Neither is pushed, and the cycles commit has to be pushed
before this tree's is, or the tree points at a commit nobody else has.**

**Watched** (`watch33.py`: every process started under the session, the
moment it appears, with its command line; `e33.py` marks the session's
steps in time). The note-taker's candidate is it:

| session | at the first document | at the second |
|---|---|---|
| staged 14:23 | `cmd.exe /c where nvcc`, a `conhost.exe` under it, `where.exe nvcc` under that | nothing |
| after | nothing | nothing |

The first document brings the first 3D view, the first 3D view probes the
path tracer's devices, and cuew ends its search for the CUDA compiler with
`popen("where nvcc")`. A process with no console gets a console WINDOW for
the shell `popen()` starts. The answer is kept, so once per session. hipew
has the same line for `hipcc`; it is not reached on this box (its library
does not load first), and was changed with it.

**Fix,** in the cycles fork (branch `LinkVibe`): both look along the PATH
with `SearchPath`, which starts nothing. The first document of the session
is made in 2.3 s where it took 3.5 s.

Left: the two `--version` calls further down in the same files still go
through `popen()`. They run only where a compiler was found, which this box
has not, so there was nothing to watch.

Evidence: `..\dl\handson\2026-10-07\entry33-*`, `watch33.py`, `e33.py`.

## 38. Omni search: the first bring-up freezes -- FIXED

`bb31f8820b`. Not staged. Asked of the build session directly, 2026-10-07:
"optimize omni search first bring up speed. right now there is an obvious
freeze time".

**Measured** (`omni1.py`: the command timed, then the event loop; the
reporter's configuration of 2026-10-07 00:13, `scanner.FCStd` open):

| | the first bring-up | the second |
|---|---|---|
| staged 14:23 | 0.99 s in the command, 0.28 s held after | 0.01 s |
| after | 0.15 s, 0.07 s | 0.01 s |
| an empty session, staged / after | 0.31 s / 0.015 s | |

**Where it went** (the main thread sampled): making the command completer's
list. Qt lays a completer's list out the moment it turns it into a popup
window and asks the delegate the list has THEN for the size of every row.
That is Qt's stock delegate -- the box's own is put on afterwards -- and the
stock one sizes a row by reading its icon. So the icon of each of the 609
commands was loaded and rendered from its SVG before the box was shown.

**Fix.** The box makes its five lists itself, with its own delegate in
place before Qt's layout. Its row size is that of its text; an icon is made
when its row is painted.

Scored: `tests/gui/omni-search-first-bring-up.py`
(`GuiOmniSearchFirstBringUp_tests_run`), 8 PASS; on the staged binaries 1
FAIL -- reading back the icon of every row after the bring-up takes 3 ms
there, because the bring-up had made them all, and 229 ms here.
`omni-search-slash-word.py` 10 PASS, `OmniSearch_Tests_run` passes.

Evidence: `..\dl\handson\2026-10-07\omni-bring-up-*`, `omni1.py`.

## 39. MSAA does not reach the view -- FIXED (found under entry 26)

`c7d115e576`. Not staged. The reporter, 2026-10-07, on entry 26: "have you
tested with scanner open and then change the msaa setting". The halt had
been tested that way; whether the anti-aliasing TOOK had not, and it did
not -- anywhere, since 2026-09-07.

**Seen** (the backend's own lines, a box in a fresh profile, Direct3D 11,
"MSAA 4x" chosen):

    bgfx: view init 1500x678 msaa 4
    bgfx: 4x MSAA scene targets could not be created on this backend --
          rebuilding without multisampling
    bgfx: view init 1500x678 msaa 1

and from then on the session draws without multisampling whatever the
preferences say: "MSAA 2x" builds 1 sample, "MSAA 8x" does not rebuild at
all. Nothing on screen says so.

**Cause.** `fd5a9a5aa6` built the scene depth readable as a texture at
every sample count, so that a frame capture need not rebuild the targets.
A MULTISAMPLED depth cannot be resolved, and bgfx refuses the framebuffer
("Frame buffer depth MSAA texture cannot be resolved"): colour made, depth
made, framebuffer refused. The view then took the fallback that exists for
backends which cannot multisample at all. The rule is bgfx's own, not
Direct3D's, so this was every backend.

**Fix.** The depth is readable without multisampling (the default) and
write-only with it. A capture of a multisampled view returns its picture
and reports `geometryPixels` as -1, unknown, instead of a count read off a
target nothing drew into. `getRenderStats()` has `msaaSamples` now: what
the targets were BUILT with. The fallback's console line names which of the
three objects was refused.

**Scored.** `tests/gui/msaa-reaches-the-view.py`
(`GuiMsaaReachesTheView_tests_run`): none, 4x, 2x, none, 4x again -- 11
PASS; 5 FAIL on the staged binaries. The reporter's case on the fixed
tree: their configuration, `scanner.FCStd` open, the anti-aliasing changed
in the dialog, OK -- 0.35 s in the click, 0.40 s held after, and both views
rebuilt at 4 samples with no fallback. `image-pixels-mode3.py` 16 PASS.
`readback-frame-mode.py`: 24 PASS and 1 FAIL ("mode1: the highlight is gone
when the mouse has left"), the same one on the staged binaries -- there
before this, not looked at.

Run on Direct3D 11 only. OpenGL, Vulkan and Metal were not.

**For entry 25** (the outline of a highlighted face is jagged, "MSAA or
not"): part of "or not" is this -- MSAA was never on. But with MSAA really
at 4 samples the outline's inner edge is jagged still
(`..\dl\handson\2026-10-07\entry25-outline\`, a cylinder's face hovered,
enlarged eight times, without and with): the outline is cut by a stencil
mark in a pass drawn after the resolve, one sample per pixel. Entry 25 is
its own thing, and not started.

Evidence: `..\dl\handson\2026-10-07\msaa-*`, `entry26-first-ok-with-msaa-*`.

## 24. Every setting behind a generated class -- C++ SIDE DONE, both rounds of the decisions applied `e21eff05a7`, `427ffc8d28`; nothing left with the reporter; pushed

The reporter, 2026-10-07, asked which entry "do entry 23 next" meant: "I
meant entry 24". Not staged.

**The size of it** (`inv24.py`, static: every `Get`/`Set<Type>("key", ...)`
with a literal key in C++ and Python, given the group of the nearest path
above it, and every preference widget of a `.ui` file; `inv24.tsv` has a
row per group and key with its readers and defaults):

- 1734 pairs of (group, key) are read, written or shown by a widget; the
  generated classes define 570.
- By where they are read: `src/Gui` 375 key names not behind a class,
  Sketcher 152, TechDraw 137, BIM 116, Part 60, Fem 52, CAM 48,
  AddonManager 34, Material 34, Start 32, App 27, Draft 26, Assembly 22,
  Import 20 ... 1075 in all.
- 511 of the pairs could not be given a group from the source text alone
  (the group handle comes from elsewhere); the count of groups is 141.
- Not every key is a setting. Window geometry, recent files, the last
  directory, a dialog's last values are STATE the program keeps for itself;
  they are not for the omni search. Each group has to be read for which is
  which.

**How a group is done** (the report view's is the worked example,
`a6ed59e824`):
1. Its keys go into the group's `*Params.py` -- one default each, settled
   where readers disagreed (entry 23's question, per key), and a short
   documentation written from the code that reads the key.
   `ParamRegistryTest.everySettingIsDocumentedBriefly` holds the text to 400
   characters.
2. The class is regenerated (`regen23.py`, line endings kept).
3. Every reader asks the class. A reader that used to be told by the group
   (`OnChange`) follows the class's `signalParamChanged` instead and applies
   a setting in ONE place, at construction and on each change -- so a change
   from the preferences, a menu, the omni search or a script all arrive the
   same way, at once. Where applying is expensive it goes through a delayed
   handler, so several changes make one apply.
4. A widget that toggles the setting stores it and lets step 3 apply it.
5. `tests/gui/preferences-ok-keeps-defaults.py` then holds the preference
   page's default to the definition's without being told about the new keys.
6. A GUI test of the group: listed by "/param", followed at once, and
   whatever the conversion put right.

**Done: the report view** (`Preferences/OutputWindow`), 12 keys: which
messages are recorded, their four colours, "Go to end", the two Python
redirections. Readers: the view, the status bar, the expression dialog, the
macro runner. Put right on the way: `checkCritical` switched NORMAL
messages off; Python's output, switched off through its key, did not come
back. Scored: `tests/gui/report-view-settings.py`
(`GuiReportViewSettings_tests_run`) 12 PASS, 5 FAIL on the staged binaries.

**Done: Part's measurements** (`Preferences/Mod/Part`), 7 keys, `014dc501f8`:
the three colours, the font, its size, bold and italic of the dimensions the
measure commands draw. A change rebuilds the measurements on screen through
one short timer; it used to wait for the next measurement or the page's
Refresh button. Put right on the way: the Measure page stored a font nobody
chose (its font box could not show "defaultFont", showed the first system
font and OK stored it -- Tahoma here); the defaults test caught it the moment
the key had a definition. Scored: `tests/gui/part-measure-settings.py`
(`GuiPartMeasureSettings_tests_run`) 5 PASS, 3 FAIL on the staged binaries.

**Done: the General group** (`Preferences/General`), 29 settings, `e0a8a17e23`:
a new generated class, `GeneralParams`. 22 reads in eleven files ask the
class. Two kinds of reader keep reading the group, on purpose: those TOLD of
a change by the parameter manager's own signal (it arrives before a class
has brought its cache up to date -- the tool bar icon sizes, the preference
widgets' auto apply), which now take the class's default; and the language
and the start workbench, whose default is not a constant. Nine keys of the
group are the program's own state and are not listed. Put right on the way:
the General page's tool tip icon size was stored in a key nothing reads;
"apply preferences at once" did not apply itself (a read where a write was
meant); the decimal separator setting's change handler was registered under
a name no key has; and the generator defined a helper of every class with a
change signal as a free function, so a second such class in one library did
not link. Scored: `tests/gui/general-settings.py`
(`GuiGeneralSettings_tests_run`) 6 PASS, 5 FAIL on the staged binaries.

**Done: the main window and the themes** (`Preferences/MainWindow`, 18
settings; `Preferences/Themes`, the three accent colours), `7b28d267df`: two
new classes, `MainWindowParams` and `ThemeParams`. Readers that read at use
ask the class (the title bar, the dock window shortcut, the tool bar areas,
the workbench selector's place, the menu style sheet, the icon set policy);
the theme machinery keeps reading the group, because it runs while a theme
is being written key by key, and takes its defaults from the class. Eight
keys of the group are the program's own state and are not listed. Put right
on the way: the second and third accent colour had FOUR defaults (the style
sheet's, the first accent's on the Theme page -- which OK stored for all
three and the next start rewrote --, the same in a saved theme, black in the
style parameter source) and have one now, held by a unit test; a change of
`GlobalToolBarArea` alone moved nothing; the "is this theme customised"
comparison read an unset `TitleBarToolBars` as off where the window reads it
as on. Scored: `tests/gui/mainwindow-settings.py`
(`GuiMainWindowSettings_tests_run`) 6 PASS; 2 PASS, 4 FAIL on the staged
binaries. LEFT, seen and not touched: a user-saved theme does not carry
`QtStyle`, `CustomTitleBar` or `TitleBarToolBars` (the template lacks them);
`StatefulLabel` waits for `StyleSheet` in the General group, where it never
is.

**Three things in the preferences dialog itself,** found by that group's
test, `2022b69697`:
- Cancel asked "Do you want to revert back to previous settings before
  exit?" on a dialog nothing was changed in -- on the staged binaries too,
  at the first Cancel of a session. The dialog took any parameter written
  while it was open for a change, and the dock windows store their layout
  when the main window first loses the focus, which is when the dialog
  opens. Only a key below `Preferences` counts now.
- The same question on EVERY Cancel was mine: the Measure page (above) made
  "defaultFont" current after the restore, and with preferences applied at
  once the font box stored the key. The box does not save while that is
  done. (Blocking its signals was the first try and was wrong: OK then
  stored Tahoma again, and the defaults test said so.)
- "Apply preferences at once" switched OFF did nothing for a dialog opened
  afterwards: a preference widget made while it is off connected its save
  all the same. `PrefWidget::autoSave()`.
`tests/gui/preferences-cancel-asks-nothing.py`
(`GuiPreferencesCancelAsksNothing_tests_run`) 3 PASS; 2 PASS, 1 FAIL staged.
`general-settings.py` has three more claims: 9 PASS; 3 PASS, 6 FAIL staged.
A page that sets a widget after its restore shows up in the first of these,
whichever page it is.

**Done: the notification area** (`Preferences/NotificationArea`), 11
settings, `5bf21a2f43`: a new class, `NotificationAreaParams`. The area, the
main window and the notify helpers ask it; the area applies each setting
when it is made and the one the class says has changed. Nothing was found
wrong in this group. Scored: `tests/gui/notification-area-settings.py`
(`GuiNotificationAreaSettings_tests_run`) 5 PASS; 3 PASS, 2 FAIL staged (the
two omni search claims).

**Done: the Python console, the macros, the dialogs**
(`Preferences/PythonConsole` 5, `Preferences/Macro` 8, `Preferences/Dialog`
2), `d5ade41454`: `PythonConsoleParams`, `MacroParams`, `DialogParams`. What
an unset `DontUseNativeDialog` means is the build's choice, and the class
has it as the default. Put right on the way: a macro path stored EMPTY was
taken for the path (the macro dialogs listed nothing,
`FreeCAD.getUserMacroDir(True)` returned ""); Draft's ShapeString panel put
the file dialog switch back to False whatever it had been, at every use; the
Macro dialog stored three hidden switches each time it read them; the
console's block cursor kept its width when the font changed. Left out as
never read: `Macro/ScriptToFile`, `Macro/ScriptFile` (the Macro page still
stores them). Scored: `tests/gui/console-macro-settings.py`
(`GuiConsoleMacroSettings_tests_run`) 12 PASS; 6 PASS, 6 FAIL staged.

**Done: the units** (`Preferences/Units`), 5 settings, `7d728d7e77`: a new
class in App, `App::UnitsParams`. This group was read at fixed moments (the
start, a document becoming active, OK on the General page), so a change from
anywhere else waited for one of them: the status bar's unit button showed
the new unit system while quantities were still formatted in the old one.
App puts the number of decimals and the inch fraction in force when they
change, Gui the unit system, by the rule activating a document already
applied. Two CAM readers took an unset unit system for 6; they say 0. The
status bar's button keeps reading the group (it is told by the group) with
the class's defaults. Scored: `tests/gui/units-settings.py`
(`GuiUnitsSettings_tests_run`) 7 PASS; 2 PASS, 5 FAIL staged.

**Done: the editors** (`Preferences/Editor`), 21 settings, `4790a2cb38`:
`EditorParams` -- font, sizes, tabs or spaces, line numbers, block cursor,
thirteen colours. The editors, the console and the report view are told by
the group and keep reading it, with the class's defaults; the colour table
is one function where there were two copies. Put right on the way: Tab
inserted a tab character and Enter indented with spaces in the same Python
editor (`Spaces` had two defaults; it is ON now, and the page no longer
shows and stores "Keep tabs" for an unset key); the Editor page showed the
first fixed-pitch font instead of the one in use and OK stored it (Courier
became Cascadia Code here); the report view's line limit, stored by its
context menu, was 10000 again after every start (read from a group that has
no such key; it is `MaxLines` of `ReportViewParams` now); two keys nothing
reads are no longer stored. DECIDED here, for the reporter to overrule:
`Spaces` on; the font default stays "Courier", what every reader used.
RETRACTED before it was committed: a reading that stored colours never
reach a macro editor opened later -- the claim passed on the staged
binaries. LEFT: `Bookmark`, `Breakpoint`, `Character` are listed and stored
by the page and dropped by the highlighter; the page stores every colour on
OK, `Text` included. Scored: `tests/gui/editor-settings.py`
(`GuiEditorSettings_tests_run`) 8 PASS; 4 PASS, 4 FAIL staged.

**Done: the rest of the document settings** (`Preferences/Document`), 10
more in `App::DocumentParams`, `670505110d`: auto recovery (on, interval,
compressed, binary shapes), recovery and a new document at startup, undo
(on, steps), view changes modifying the document, the JSON indentation. Put
right on the way: auto recovery was set up at the start and by OK on the
Document page only, and follows its settings now; the auto saver read
"save thumbnails" as off where its default is on, so with the key not stored
it did not switch them off while it saved, which is the one thing it reads
the key for; the page stored two keys nothing reads, from two check boxes it
hides. Scored: `tests/gui/document-settings.py`
(`GuiDocumentSettings_tests_run`) 4 PASS; 1 PASS, 3 FAIL staged. The timer,
the thumbnail switch and the indentation are from the code. LEFT: the auto
saver still writes the thumbnail key while it saves, and a parameter written
while the preferences are open makes Cancel ask about reverting.

**Done, first step: the View group** (`Preferences/View`), 61 more
settings in `ViewParams`, `e7423066d8`: the 3D view's display, background,
lights, navigation, and seven others. They were read at about a hundred
places in 25 files with a default at each. Most reach the views through
`View3DSettings`, one observer per view, told by the group itself: it keeps
reading the group, and every read takes its DEFAULT from the class (done by
a script over the sites, `edit24s.py`). Where defaults disagreed:
- the zoom step was 0.2 where a view is made and 0 where an open view is
  told of a change: a step stored and then removed left the open views
  unable to zoom until the next start;
- Home read an unset camera orientation as Top, a new document opens in
  Trimetric. DECIDED: Trimetric, for the reporter to overrule;
- the Colors page showed, and OK stored, a background colour and a gradient
  colour the views do not draw for unset keys (20,20,163 for 234,229,220);
- the Sketcher read the background as white and without gradient.
Scored: `tests/gui/view-settings.py` (`GuiViewSettings_tests_run`) 6 PASS;
1 PASS, 5 FAIL staged. NOT DONE in this group: the 11 Sketcher keys kept in
it; 19 keys that WERE defined and are still read directly at 119 places
(`MarkerSize` at 22); the Python readers (Draft, BIM, Tux), three with other
defaults; the three colours of the default appearance, whose default is the
material card's.

**Done, second step of the View group,** `d748e690c0`: keys that WERE
defined and were still read with another default. The marker size is 9 by
definition and on the 3D View page, and was 4 in CAM, 5 in Robot, 7 in
Mesh's defect views and in the Sketcher while the key was not stored: the
markers of a sketch grew from 7 to 9 pixels at the first OK in the
preferences. 28 reads in 12 files take the class's default. Kept on
purpose: a new sketch's vertices are 4 pixels where the shape point size was
never set (the setting's default is 2). No test of its own -- nothing a
script can see says how large a marker is drawn.

**Done: the navigation cube** (`Preferences/NaviCube`), 28 settings,
`2b9cd20ae7`: `NaviCubeParams`. Its colours are stored in Qt's order
(0xAARRGGBB), so they are listed as numbers. Nothing found wrong. Scored:
`tests/gui/navicube-settings.py` (`GuiNaviCubeSettings_tests_run`) 4 PASS;
2 PASS, 2 FAIL staged.

**Done: PartDesign** (`Preferences/Mod/PartDesign`), 11 settings,
`8ee1957f20`: a new class in its App library, `PartDesign::PartDesignParams`
(features read `RefineModel`). Nothing found wrong. Seen and left: Part
defines a `DefaultDatumColor` of its own in `Mod/Part` while PartDesign's
datums read this group's; `singleClickFeatureSelect` is a key of
`Preferences/Selection`. Scored: `tests/gui/partdesign-settings.py`
(`GuiPartDesignSettings_tests_run`) 5 PASS; 3 PASS, 2 FAIL staged.

**Done: Part's Boolean and geometry check options,** 22 settings,
`a11d735f1e`: the fifteen options of Check Geometry
(`Mod/Part/CheckGeometry`), the three of Part's Booleans
(`Mod/Part/Boolean`), `AutoElementMap` and two single keys, in the two
`PartParams` classes, kept in sub-groups. Put right on the way: the
"Single-threaded" box of the Check Geometry panel stored one key and the
check read another, which nothing writes -- the box did nothing; the
defaults test never compared a setting kept in a sub-group, and when it did
it named Mesh's import/export page, which showed the asymptote size empty
and stored that where the default is 500. Scored:
`tests/gui/part-options-settings.py` (`GuiPartOptionsSettings_tests_run`)
3 PASS; 1 PASS, 2 FAIL staged. (The import and export settings followed,
below.)

**Done: several small groups,** 14 settings, `a2c9d65aea`: `MiscParams`,
one class reaching into each group (recent macros, gizmos, cache directory,
shortcut timeout, workbench tab bar, the two start-up switches, the
dependency graph). Put right: the size of the recent macros menu was 12, 4
and 0 at its three readers; the shortcut timeout became 0 when its key was
removed. Scored: `tests/gui/misc-settings.py` (`GuiMiscSettings_tests_run`)
3 PASS; 3 FAIL staged.

**The full suites,** run 2026-10-08 00:25 on `a11d735f1e` (sixteen groups
in): C++ 784 of 784 (9 disabled, 1 skipped), Python 3385 tests with 2
failures -- both `TestThickness` 5829 cases, failing since the OCCT merge
and not of this work. Logs: `..\dl\handson\2026-10-07\entry24-evening\`.
The GUI tests registered in `tests/gui/CMakeLists.txt` are not part of that
`ctest` on this tree; each was run by hand with its group.

**Read, not yet converted, two more modules** (two read-only helper agents,
2026-10-08; `entry24-inventory-Sketcher.txt`, `entry24-inventory-TechDraw.txt`):
- Sketcher: about 175 keys in 12 groups, 306 sites in 28 files. 23
  findings, among them: the Grid page's "Grid spacing" is stored under one
  name and read under another, so it does not reach new sketches;
  `UseSystemDecimals` is on to its reader and shown off by its page; the
  external geometry colour differs between reader and page; the solver box
  writes three redundant-solver parameters under the wrong key when the two
  solvers differ; the Dimension tool reads the geometry tools' "continue"
  switch; the Snap command's cached state is wrong until the key changes.
  All from reading, none measured yet.
- TechDraw: 141 keys in 14 groups, 176 sites in 38 files, 103 on pages.

**Done, first step: the Sketcher** (`Preferences/Mod/Sketcher`, the group
itself), 23 settings, `97444426c5`: a new class in its App library,
`Sketcher::SketcherParams`; 61 reads and 5 writes in 12 files ask it. Put
right: "Use system decimals" is on to the program and was shown off, and
stored off on OK, by the Display page. Scored:
`tests/gui/sketcher-settings.py` (`GuiSketcherSettings_tests_run`) 4 PASS;
2 PASS, 2 FAIL staged; the Sketcher's own suites pass (ctest 111, Python
145).

**Done, second step: the Sketcher's sub-groups,** 67 settings, `721dfca8a0`:
`General` (the edit view, the grid, the rendering order, dimension editing),
`View` (line widths and patterns), `dimensioning`, `Snap`, `Constraints`,
`Commands`, `Tools`, `Elements`, in the same class under their sub-groups.
A sketch in edit is told of a change by the groups themselves and keeps
reading them; 97 reads in 14 files take their DEFAULT from the class. Put
right: the internal face colour was one step more opaque on the Appearance
page than the program draws it; the defaults test compared whole numbers
with a fraction's tolerance, which let a neighbouring colour pass -- exact
now, and this colour is the only one it named in all the groups done.
Scored: `tests/gui/sketcher-settings.py` 7 PASS; 3 PASS, 4 FAIL staged.

**The Grid page's spacing reaches a new sketch,** `dec07614e9`: the page
stores the number `GridSize`; a new sketch read the text `Hist0`, which
nothing writes any more, and started at 10 mm whatever the page said.
Measured on the staged binaries: a spacing of 25 stored the page's way gives
10. Fixed, the old profile's `Hist0` still counting while the page's key is
not stored; `sketcher-settings.py` holds it (8 PASS; 3 PASS, 5 FAIL staged).

**Done, third step: the Sketcher's keys in the 3D view's group,** 27
settings, `5ba1afc503`: its label font, a few sizes and the colours of a
sketch in and out of edit, in the same class under `Preferences/View`. A
sketch in edit took each colour from a constant of its own; it takes the
class's now, which is what the Appearance page shows. Put right: external
geometry is drawn in 204,51,153 while its key is not stored and the page
showed, and OK stored, 204,51,115. The defaults test did not follow a
setting kept in another group than its class's; it does, and names that
colour on the staged binaries. `sketcher-settings.py` 9 PASS; 3 PASS, 6
FAIL staged.

**Done, fourth step: the Sketcher's solver settings,** 27 keys,
`2c4b0159d6`: the sub-group `SolverAdvanced`, set in the "Advanced solver
control" box of the task panel. Three slips of that box, the first measured
on both builds, the others from the code: a parameter of the solver that
looks for REDUNDANT constraints was stored by which solver the MAIN combo
box names (typed for DogLeg, stored as Levenberg-Marquardt's); that
solver's tau was set from eps1; unticking its "sketch size multiplier"
switched it on. `tests/gui/sketcher-solver-settings.py`
(`GuiSketcherSolverSettings_tests_run`) 3 PASS; 1 PASS, 2 FAIL before, here
and staged alike.

**The Snap button,** `9ccbe547b8`: in a session that begins with snapping
stored off, the first click did nothing (the command's remembered state
started as "on" whatever was stored). Measured with a seeded profile, here
and staged; fixed. A first test of mine set the key after the program had
started and passed with and without the fix, so it was dropped: the fault
needs the key stored BEFORE the start, which `tests/gui` cannot arrange
yet. The probe and its profile are in the evidence directory.

NOT DONE in the Sketcher, and for the reporter to say: the Dimension tool
reads the geometry tools' "continue" switch, not the constraint tools'
(as upstream does); the key meant to remember radius or diameter in the
group button, `CurRadDiaCons`, is never stored (its path and name are one
string by a missing comma); the label font size and the constraint symbol
size have the application font's height for a default and are not listed.

**Done: TechDraw, its General group** (`Preferences/Mod/TechDraw/General`), 35
settings, `c50d40e3c7`: a new class in its App library,
`TechDraw::TechDrawParams`. TechDraw reads through hand-written accessors
and at many places straight from the group; 38 reads in 13 files take the
class's default. Where readers and pages disagreed the default is the
READER's and the page shows it -- DECIDED so, for the reporter to overrule:
the new face finder (off to the program, on on the page), the vertex scale
(3 against 5), the template mark size (5 against 3). The defaults test
loads TechDraw now and names the three on the staged binaries. Scored:
`tests/gui/techdraw-settings.py` (`GuiTechDrawSettings_tests_run`) 4 PASS;
2 PASS, 2 FAIL staged.

**Done, TechDraw's other groups,** `07bde3b18b` (46 settings whose default
is a plain value: Decorations, Dimensions, HLR, PAT, Labels, LeaderLine,
Rez, Tracker, debug) and `c8a6805a4f` (47: the colours, the line keys, the
file names). 128 of TechDraw are listed. The inventory's findings were
MEASURED on the staged binaries first (kept in
`..\dl\handson\2026-10-08\entry24-techdraw`, with a picture before and
after); what was confirmed and fixed:

- a new view on a profile that never stored a face colour has CYAN faces:
  the default was written 0xFFFFFF, which as a packed colour is 0x00FFFFFF.
  White now, which the page shows;
- the iso line count and the ISO line spacing of the pages never arrived:
  one is stored as an Int and was read as a Bool, the other stored as a
  Float and read as an Int;
- "Use Polygon Approximation" and the "Leaderline" colour never arrived:
  each page stored one key and the program read another. The pages store
  the key that is read; what they stored before is still honoured;
- the section dialog kept its two keys in a group of its own at the top of
  the user configuration, reached through a path with two colons;
- seven more pages showed a default the program does not use and stored it
  at OK (centre marks, tolerance text size, page view background, the two
  hatch colours, the line group, the section line standard); the light
  text colour showed the palette's. Reader's default each time -- DECIDED
  so, as before.

`ec6ddedbde`, found on the way by the defaults test: a unit spin box of ANY
preference page rounded its default to a whole number (`toUInt()`), so OK
stored TechDraw's arrow size as 4.0 where everything reads 3.5.

Scored: `tests/gui/techdraw-settings.py` 15 PASS; 3 PASS, 12 FAIL staged.
The defaults test passes whole on the tree and names twelve of TechDraw on
the staged binaries.

NOT fixed in TechDraw, a decision for the reporter: the Annotation page's
section, highlight, hidden and centre line STYLES. The page stores one set
of keys (LineStyleSection, LineStyleHighlight, ...), drawing reads another
(SectionLine, HighlightStyle, HiddenLine, CenterLine, CenterLineStyle),
and reads it in one place as a line of the standard and in another as a
pen style. Two of the page's four lists reach nothing. The keys that are
read are listed with their meaning, so they can at least be set. Also
left: DefaultPageScale beside DefaultScale; the projection angle "Page",
read as first angle; TechDraw's own defaults for the 3D view's highlight
and selection colours, which are not the view's. Not listed: the diameter
symbol (default outside ASCII), the two selection colours (default follows
the 3D view).

**Done: Part's import and export settings,** 26 settings, `632fc1bb30`:
the STEP, IGES and glTF translators (`Mod/Part/General`, `IGES`, `STEP`,
`Mod/Import`), in `PartParams`. They were read through three hand-written
settings classes and a second time where Part hands them to the kernel and
where Import writes a file; all take the definition's default. One page
disagreed: a STEP file exported on a clean profile names 'Author' in its
header while the page showed an empty field and stored that at OK (measured
staged). Reader's, as before. `part-options-settings.py` 6 PASS; 2 PASS, 4
FAIL staged. Left: the DXF options (they live with Draft's settings).

**Done: the small modules,** `b823244541`, from a helper agent's inventory
of what their C++ still read directly
(`..\dl\handson\2026-10-08\entry24-inventory-modules.json`, 122 keys, 92
settings): new classes `Assembly::AssemblyParams` (6),
`Points::PointsParams` (3), `Fem::FemParams` (7, only what C++ reads); 3
more in `MeshParams`, 3 in `SheetParams`, and TechDraw's diameter symbol.
The generator escapes a string default now (quote, backslash, anything not
ASCII as octal escapes); its helper sits at the END of `params_utils.py`
because the generated sources name the line each part comes from -- a line
added near the top rewrote all 66 of them. Put right: opening the
preferences with Fem loaded STORED a setting (its VTK page saved where it
loads), and Cancel then asked about reverting -- measured staged with
`preferences-cancel-asks-nothing.py`, which loads Fem now.

**The omni search missed every module loaded after its first use,**
`a4d495b72e`: its list of settings was a copy of the registry made when the
box was set up, with a `refresh()` nothing called. TechDraw, Mesh, Fem --
whatever was first switched to after one search -- was not listed. Found by
`tests/gui/module-settings.py`, which loads a module and then asks, ten
times over.

**Done: Start and CAM,** `cafb223d59`: `Start::StartParams` (15; the Start
page has no preference page, so none could be set but by hand) and
`Path::CAMParams` (13, only what C++ reads). Put right: CAM's cycle time
estimate read `WarningsSuppressAllSpeeds` where the Advanced page stores
`WarningSuppressAllSpeeds`, so its warning was suppressed whatever the page
said (measured staged). The probe move colour is the reader's (255,235,0;
the page showed 255,255,5).

**Done: Material,** `f71b8365ab`: `Materials::MaterialParams`, 22 settings
in six sub-groups. "Show legacy files" of the editor is off to the program
and was shown on by the page; reader's.

`tests/gui/module-settings.py` (`GuiModuleSettings_tests_run`) holds all of
these: 10 PASS; 10 FAIL staged. The defaults test loads Fem, Assembly and
Material as well and passes whole; it named Material's and STEP's on the
staged binaries.

**The full suites again,** run 2026-10-08 06:20 on the tree of `f71b8365ab`:
C++ 784 of 784 (9 disabled, 1 skipped), Python 3385 tests with the same 2
failures as before -- the `TestThickness` 5829 cases, failing since the
OCCT merge. Logs: `..\dl\handson\2026-10-08\entry24-suites\`.

**WHERE ENTRY 24 STANDS, 2026-10-08:** the C++ side is done -- about 1170
settings listed. What is left is the Python-only modules (below) and the
decisions. The reporter, 2026-10-08: "Next session we go through the list
and make all the decisions". THE LIST:
`..\dl\handson\2026-10-08\entry24-decisions.md` -- 28 defaults chosen
(A), 23 behaviours put right on the way (B), 5 questions never answered
(C), 16 findings not fixed (D). Nothing is pushed or restaged.

**The generator,** `0a94fb63c9`: a setting stored under another name than
its own (`param_name`) was read and written under its key, but a CHANGE was
looked for, and the key removed, under the setting's name. Mesh's two
asymptote sizes never followed a change for it. Needed for the editor's
colours, whose keys contain spaces.
`MeshParamsTest.aSettingStoredUnderAnotherNameIsFollowedAndRemoved` failed
on both counts before.

**Two things about the tools,** both cost a run or more:
- A `.ui` edit reaches the binary one build late here: the first `ninja`
  regenerates `ui_X.h` and does not recompile `X.cpp`. The Theme page showed
  its old colours on a "freshly built" tree. Build twice after a `.ui` edit.
- A test that presses Cancel from a timer has to send the click from a
  timer of its own, and look the button up there: on Windows a timer does
  not fire again while its slot is inside the box the click raised.

**Read, not yet converted** (four read-only helper agents; their tables are
in `..\dl\handson\2026-10-07\`, `entry24-inventory-*.txt`):
- `View`, what is left of it: see above. Seen and not touched: any change
  of a key of the View group that `View3DSettings` does not name re-applies
  the background colours to every view.

**Order from here,** by what a user meets first: the rest of `src/Gui`
(small groups: the gizmos, the cache directory, the property view, recent macros), then Part and PartDesign, the Sketcher, TechDraw, and the rest.

**To decide, for the reporter:**
- Modules written in Python only (BIM, Draft, AddonManager, parts of CAM
  and Fem) have no generated class to register from: about 300 keys. The
  registry can be given a Python door (a module registers its settings from
  a definition file at import), or these wait. Which?
- State keys (above) are left out unless said otherwise.
- "Apply the change with delay handler": done as in step 3 -- at once where
  it is cheap, through `ParamHandlers::addDelayedHandler` where it is not.
  Say if every change is to be delayed.

Evidence: `..\dl\handson\2026-10-07\inv24.py`, `inv24.tsv`,
`inv24-summary.txt`, `entry24-report-view-*`.

**THE DECISIONS, 2026-10-08.** The reporter went through the list
(`..\dl\handson\2026-10-08\entry24-decisions.md`) and answered in one
message, in their words:

> "apply preferences at once" applies itself, what did you do with it. For
> the rest you changed, lookup upstream code and follow their behavior. If
> they also has descripency, follow their program default. C1 python gate.
> C2 use generator all the same. C3 no delay for cheap one. C4 yes. C5 one
> push. D1, D2 check upstream first. D3 default to all 0 and follow 3d,
> Otherwise use the setting. D4 explain what's continue switch. D5, D6
> fix. D7 fix. D8 elaborate. D9-12 fix. D13 elaborate. D14 expose the
> assembly setting. What's with material and import. D15, 16 fix

Applied in `e21eff05a7`. Upstream is `upstream/main` `b960974504`
(2026-10-01), read by four helper agents and checked at each place that was
changed.

*A, the defaults.* Rule: upstream's behaviour, and where upstream's page
and program disagree, upstream's program.

| # | Upstream's program | Result |
|---|---|---|
| A1 | spaces on, at all three readers | as it was |
| A2 | no family stored = the system's fixed-pitch font | CHANGED from "Courier"; an editor is 15 pt on Linux again; a text document shows in the application font until a font is stored |
| A3 | Home = Top, a new document = Trimetric, on purpose | CHANGED back: Home is Top |
| A4 | 234,229,220 ... (page 20,20,163) | as it was |
| A5 | 4 CAM, 5 Robot, 7 Mesh defects and sketch, 9 elsewhere | CHANGED back to those four |
| A6 | on (page off) | as it was |
| A7 | another key and colour (`SketchFaceColor`, orange, alpha 64) | as it was: `FaceColor` is the fork's own key, blue on purpose (`94d76ec9c2`) |
| A8 | 204,51,153 (page 204,51,115) | as it was |
| A9 | width 500, height EMPTY | CHANGED: height empty, `size(500);` |
| A10-A19 | each the reader's value | as they were. A15, A16: upstream's literal has alpha 0 where the fork has 255; not copied, it would paint a transparent brush |
| A20-A22 | 'Author'; 255,235,0; off | as they were |
| A23 | white | as it was |
| A24 | 0 (black) at the only reader; page and first-start seed 0,171,255 / 85,123,182 / 85,123,182 | NOT changed; the fork's three are its own Light theme's. Back to the reporter |
| A25 | 12 in effect | as it was |
| A26 | 6 at CAM's reader (0 in core) | CHANGED back to 6; the tool bit editor takes upstream's `FreeCAD.Units.getSchema()` |
| A27 | 0xC8FFFF00, last byte never read | as it was (the same colour) |
| A28 | 16, with a newer layout of the file cards | NOT changed: the fork's file card view is upstream's older one, where reader and layout were both 20. Back to the reporter |

*B, the faults put right.* None reverted. By the helpers' reading of
upstream (their quotes, not run): upstream has the same fault in
B3, B7 (`checkCritical`), B9 (all four), B10, B11, B12, B14, B16 (all
three), B17, B18 (`IsoCount`), B19 and B21. Fork-only code: B1, B4, B5,
B8, B13, B20, and B6, B22, B23 (the fork's own machinery). Not faulty
upstream, and the fix gives upstream's behaviour: B2, B15 (upstream's
spin box stores text where the fork's stores a number). B5, which the
reporter asked about: "apply preferences at once" exists in this fork
only; `PrefParam::setAutoSave` called `GetBool` where it meant `SetBool`,
so unticking the box did not reach the preference widgets until OK.

*C.* C1: the Python door. C2: state keys go through the generator like
the rest. C3: no delay where applying is cheap (as done). C4: entry 24 is
done with the C++ side; the Python-only modules are an entry of their own.
C5: one push. NOT STARTED: C1 and C2 are sweeps and get a count and a cut
first (`size-and-decide-before-sweeping`). `left24.py src/` cannot give
C2's count: it also lists defined keys whose readers still read the group.

*D.*
- D1 checked, NOT changed, back to the reporter. Upstream wired the
  SECTION style list to drawing (`LineStyleSection`, default line 4, the
  default of views made afterwards); the fork draws from `SectionLine`
  (a pen style, default 2) and uses the property once as a pen style and
  once as a line number. HIGHLIGHT is unconnected in both trees.
  Hidden and centre DO reach drawing in the fork, by the helper's
  reading (the list said two of four reach nothing; it is section and
  highlight). Following upstream moves a new view's section line from line
  2 to line 4 and changes what the saved property means.
- D2: `DefaultPageScale` dangles upstream too, left. The projection
  angle is fixed the way upstream means it: "Page" makes a new
  projection group follow its page; a new page stays first angle
  (upstream leaves its two-entry enumeration at 2, which is no value).
- D3 done as said: `PreSelectColor` and `SelectColor` are listed, 0 while
  not set, 0 follows the 3D view's setting with the 3D view's default.
  The Colors page shows the view's colour and stores nothing until
  another is chosen.
- D4 explained, nothing to change: upstream's Dimension tool reads
  `ContinuousCreationMode` too.
- D5 fixed, and a second fault behind it: `GroupCommand::createAction`
  read the stored choice and dropped it.
- D6 fixed: both listed, 0 = the application font's height.
- D7 fixed (the template). D9 fixed. D11 fixed.
- D8 explained, NOT changed: `StatefulLabel` (the solver message of the
  Sketcher's task panel) listens for `StyleSheet` in `Preferences/General`;
  the key is in `Preferences/MainWindow`. Its per-state style cache is
  therefore not cleared when the style sheet changes. Same upstream. One
  line to fix.
- D10 fixed, and it was more than the write: in this fork a save follows
  the document's `SaveThumbnail` property, so the switch the auto saver
  flipped reached nothing and every recovery save rendered a thumbnail.
  The writer is told (`NoThumbnailUpdate`). From the code.
- D12 fixed: one key, `Mod/PartDesign/DefaultDatumColor`, defined by
  Part's class because the sub-shape binder lives in Part.
  `DefaultDatumLineColor` moved with it. A value stored in `Mod/Part` is
  moved once when PartGui loads.
- D13 explained, NOT changed: a hidden per-extension override from the
  old web start page (upstream `79ea979eb1`, 2023, "later we can set up
  an UI"), gone upstream with that page. The fork's new Start reads it
  (`de27349da7`); nothing has ever written it, and the comment beside the
  read says the import dialog does.
- D14: Assembly's colour is `Mod/Assembly/JointHighlightColor`, default
  the red. It no longer follows a stored `View/HighlightColor`. Material
  and Import: both are App libraries and cannot see Gui's `ViewParams`,
  so they read the View group with literals of their own, as upstream
  does. Import's face colour while not stored is 204,204,204; the 3D
  view's in this fork is 204,204,230. Material's defaults are
  `App::MaterialAppearance::DEFAULT`'s. NOT changed.
- D15 fixed. D16 fixed.

Scored: `tests/gui/entry24-decisions.py`
(`GuiEntry24Decisions_tests_run`) 12 PASS; 0 PASS, 12 FAIL staged. The 22
settings GUI tests together 196 PASS, 0 FAIL
(`..\dl\handson\2026-10-08\entry24-scripts\g-d-*`). D10 and D11 from the
code. The helper `guard.py` of `gui.cmd` was gone with a session scratch
directory and is written again, beside `gui.cmd`.

The full suites on the tree of `e21eff05a7`, 2026-10-08 08:53: C++ 784 of
784 (8 disabled benchmarks did not run), Python 3385 tests with the same 2
failures as before, the `TestThickness` 5829 cases. Logs:
`..\dl\handson\2026-10-08\entry24-scripts\full-ctest.log`, `full-pytest.log`.

**BACK TO THE REPORTER:**
- A24: accent colours 2 and 3 -- the fork's own (kept), upstream's seed
  (85,123,182 for both), or upstream's literal reader default (black)?
- A28: file card spacing 20 (kept) or upstream's 16?
- D1: follow upstream for the section line style list?
- D8: fix the one line?
- D13: drop the read, keep it as a hidden key with a true comment, or
  have the import chooser store the choice?
- D14: should Import's default face colour be the 3D view's?
- Seen by the helpers, not touched: TechDraw `ScrubCount` is 1 upstream
  and 0 here (0 before entry 24 too); upstream replaced `NewFaceFinder`
  by `FaceFinderVersion` with a third finder, not in the fork.

**THE SECOND ROUND, 2026-10-08.** The reporter answered the questions that
had gone back, in their words:

> D4 keep as upstream. D8 Fix. D13 drop as upstream. D14 Migrate default
> face color to Material setting. A2 default to Courier. A9 Unify the
> default to 7. A24 use upstream seed. A28 make it 16. D1 follow upstream.
> D2 you mean DefaultPageScale has no user? then drop it. D12 keep it in
> Mode/Part, drop the partdesign one. D14, is that what upstream does.
> follow upstream for this one.  Entry 42, list those keys. Push after done

Applied in `427ffc8d28`:

- A2: the editors' font is "Courier" again while it is not set, an
  editor is 10 pt on every platform again and a text document shows in the
  editors' font again -- the state before the first round. What stays of
  the first round: `Gui::editorFont()` (an EMPTY family is the system's
  fixed-pitch font) and the Editor page storing a family only once one is
  chosen.
- "A9 Unify the default to 7": READ AS A5, the marker size -- A9 is the
  Asymptote size, which has no 7 in it, and A5 is the one item whose
  readers had 4, 5, 7 and 9. Said so to the reporter. The setting's
  default is 7 and every reader takes it: the 3D view's markers, CAM,
  Robot, Mesh, the Sketcher, and Draft's own table of the View defaults.
  The 3D View page shows 7px for an unset key. The Asymptote height stays
  empty, as decided in the first round.
- A24: NOT APPLIED, back to the reporter with what the question had left
  out. The fork gave the three accent slots three colours on purpose
  (`00d2b684fd`, "give the three accent slots three colours"): the style
  sheets use slot 2 for focus and pressed and slot 3 as the far stop of a
  gradient from slot 1, and with upstream's seed -- one colour for 2 and 3,
  the same as 1 here -- focus paints what hover paints and the gradients
  go flat. `Application::checkForDeprecatedSettings` rewrites a stored trio
  of exactly that seed at every start. Taking the seed as the default
  undoes that commit for a profile that stored nothing.
- A28: the file card spacing is 16, and the file card view takes its
  spacing from the setting where 20 was written.
- D1: a new view's section line is the line of the standard that the
  Annotation page's list names (`LineStyleSection`, default line 4), as
  upstream. The port was small: drawing already took the property for a
  line number (the pen style set before it was overwritten two lines
  later); only the property's default came from the pen style key
  `SectionLine`, which no page stores. That key, its reader
  `PreferencesGui::sectionLineStyle` and three functions nothing else
  called are gone. Views in saved documents keep their line.
- D2: `DefaultPageScale` is gone; "page" as the scale type gives the scale
  of new pages (`DefaultScale`), which is what the Scale page stores.
- D8: `StatefulLabel` listens in `Preferences/MainWindow`.
- D12, the other way round from the first round: ONE key,
  `Mod/Part/DefaultDatumColor`, and `DefaultDatumLineColor` beside it.
  PartDesign's is dropped; a value stored there is moved once when
  PartGui loads.
- D13: Start no longer reads `DefaultImport<extension>`.
- D14, Import: the default face colour of an import and an export while
  `View/DefaultShapeColor` is not stored is the colour of Material's
  default appearance (`App::MaterialAppearance::DEFAULT`, 204,204,230),
  which is also the 3D view's; a grey of Import's own was written there.
  Not `MaterialManager::defaultAppearance()`: that one answers with a
  random colour when "random colour" is on.
- D14, Assembly: upstream's rule again -- the 3D view's preselection
  colour once that is stored, the red until then. The setting
  `JointHighlightColor` stays, 0 while not set, and 0 is that rule.
- D4: nothing to change.
- Entry 42: the state keys are to be LISTED by the omni search. Not
  started.

Scored: `tests/gui/entry24-decisions.py` 13 PASS (one claim more: the
section line of a new view is line 4, and line 2 with the list stored at
its second entry); 3 PASS, 10 FAIL staged. The 22 settings GUI tests:
197 PASS, 0 FAIL.

The tests' row collection changed with it. The first chain of this round
failed every "/param" claim but the first of each test: the reporter was
working at the desktop, the test window lost activation after its first
query, and the omni search's list is not shown then -- the rows were
there (`entry24-decisions-work\omni-probe.py`: 3 and 2 rows in a list
that is not visible). `param_rows` of 18 tests and `param_titles` of one
take the rows of a list that is not shown too.

The full suites on the tree of `427ffc8d28`: C++ 784 of 784, Python 3385
tests with the same 2 `TestThickness` 5829 failures. Logs as before,
`..\dl\handson\2026-10-08\entry24-scripts\full-ctest.log`, `full-pytest.log`.

Seen on the way by the reporter, 2026-10-08: "the omni search list box's
highlighted text color is white, which does not look good with light blue
highlight background. is this the side affect of the theme default
setting change?" It is not: the row is painted by the style (the style
sheets' `@AccentBackgroundColor`, a light blend of accent 1, whose default
entry 24 did not change), and the delegate takes the palette's
`HighlightedText` for the text (`OmniSearchBox.cpp`, the row delegate).
Neither changed since the stage of 2026-10-07. NOT fixed; it wants an
entry of its own.

**STILL WITH THE REPORTER after the second round:**
- A24, with what the question left out (above): keep the three colours of
  `00d2b684fd`, or really take upstream's one colour for slots 2 and 3?
- "A9 Unify the default to 7" was taken for A5, the marker size. Say if
  it meant something else.
- The omni search's highlighted row, white on light blue: an entry of its
  own?

**Answered, 2026-10-08,** in the reporter's words: "A24, keep ours then.
don't change. and yes it is A5 marker size." So the three accent colours
of `00d2b684fd` stay, the marker size is 7 everywhere as applied, and the
highlighted row is entry 43 of the queue. Entry 24 has nothing left with
the reporter. Pushed: `origin/PartDesignPort` = `b70cc6ebf1`, cycles
`35a3bd898` first. Not staged.

## 41. The Python-only modules' settings, through a door into the registry -- FIXED, all four steps (the way in, Assembly; Draft and BIM; Fem, CAM, the Addon Manager; the small rest); the whole list answered and carried out

Decided under entry 24 (C1, C4). Not started. Sized 2026-10-08, static,
with `inv24.py` run again on the tree of `b70cc6ebf1` and split by who
reads a key (`..\dl\handson\2026-10-08\entry41-42-sizing\`: `inv24.tsv`,
`split.py`, `refine.py`, `py-undefined.tsv`, `ui-undefined.tsv`).

About 600 keys, where "about 300" was said under entry 24 -- that number
had missed what Draft and BIM read through Draft's own table and what a
page stores without a literal read beside it:

- Draft and BIM, about 440: 234 on their preference pages, 155 in the
  table of `draftutils/params.py` and on no page, about 50 read by BIM
  with the key written out. Draft's table holds name, type and default for
  its 155 and builds the page ones from the `.ui` files when it is first
  asked: it is a definition file already, in another form.
- Fem's Python side, about 54. CAM's, about 40 (23 of them named in
  `Path/Preferences.py`). AddonManager, about 20, with a defaults file of
  its own. Assembly's Python page, 14. Help, 14. OpenSCAD, 11.
  ReverseEngineering, 11 (a C++ module whose page stores keys no literal
  read was found for). Tux, Material, Test: about 12.

These are counts of (group, key) pairs from the sources, not of settings
checked one by one; the readers that name a key through a variable are
not seen at all.

The cut put to the reporter, 2026-10-08: (1) the door -- a call that
registers a setting from Python and a loader for definition files in the
format the C++ classes use -- proved on Assembly's Python page; (2) Draft
and BIM through Draft's own table, not a second copy of it, with a short
text for the about 155 that have none; (3) Fem, CAM, AddonManager; (4) the
small rest. Registration only: the Python readers keep their code, and a
default that a reader and a page disagree on is listed for the reporter,
not chosen. The reporter: "start in next session. 41, looks good, go
first."

**Step 1, the way in, and Assembly through it** (`a75b43f1d5`), 2026-10-08:

- `App::ParamSpec` and `ParamRegistry::add(spec)`: a description put
  together at run time, whose strings the registry keeps. Refused: an empty
  path or entry, a default that is no value of the type, a path and entry
  described already (the first description stands; the entries handed out
  are held by pointer).
- `FreeCAD.registerParam(path, entry, type, default, title=, doc=, ...)`
  and `FreeCAD.listParams(query='')`. The default is a Python value of the
  type. A path that names no parameter set raises: every list of settings
  reads a value through the path, so one bad path would break them all.
- `freecad.params` (`src/Ext/freecad/params.py`) loads a definition file in
  the form the generated classes use, with the generator's own classes:
  `Tools/params_utils.py` is installed beside it and imported with an empty
  module standing in for `cog`.
- Assembly: `Mod/Assembly/AssemblyPyParams.py`, imported by its `Init.py`,
  the 13 settings only its Python code reads (the count had said 14). Listed
  from the start of a session, the module not loaded. Registration only;
  `AssemblyTests/TestSettings.py` reads the sources and holds each described
  default to the one every reader passes.

Scored: `Tests_run` `ParamRegistry*` 9/9 (2 new); `-t BaseTests` 57 OK (7
new); `-t TestAssemblyWorkbench` 16 OK (2 new);
`tests/gui/python-settings-door.py` (`GuiPythonSettingsDoor_tests_run`) 5
PASS, 0 PASS and 5 FAIL on the staged binaries. Described in
`docs/OmniSearch.md`, section 3.1.

**Step 2, Draft and BIM** (`4a99a978f7`), 2026-10-08: 426 settings, through Draft's own
table. `draftutils/params.py` gets three lines: it notes the widget of each
page setting while it reads the pages, and hands its table to the new
`draftutils/params_registry.py` when it is loaded. That module adds what a
description needs beyond the table's name, type and default:

- of a page setting (226), the title -- the text of a check box, or the
  label beside the widget -- the tool tip, and the editor: the items of the
  18 combo boxes, the range of the spin boxes, colour buttons, file
  choosers;
- a written title and documentation for the 153 settings of the table no
  page shows, and a documentation for the 47 page settings without a tool
  tip (the in-command shortcuts, the default colours and sizes of BIM's
  objects); one tool tip was over the 400 characters (`DWGConversion`);
- BIM's settings the table does not have: the 17 of the NativeIFC page,
  which the table does not read, and 30 that BIM's code reads with the name
  written out (dialogue sizes, the library panel, the views manager).

Only Draft's and BIM's own groups are described; the table also holds 21
settings of other groups that Draft reads (General, Units, View, Mod/Mesh),
which are their modules' to describe. Measured before deciding where to
register: loading the table costs 1.9 s here (0.49 s `Draft_rc`, 0.57 s
`Arch_rc`, 0.85 s the table), so it is NOT done at the start of a session --
Draft's and BIM's settings are listed once either workbench has been used,
as a C++ module's are when its library loads.

`drafttests/test_params_registry.py` (in `TestDraft`): every own row of the
table is in the registry with the table's type and default; every setting
Draft and BIM describe has a title and at most 400 characters of
documentation, so a setting added to the table without either fails; the
written documentation names nothing that is not there; and each default
written for a directly read setting is the one every reader in BIM's
sources passes.

Scored: `-t TestDraft` 91 OK (6 new), `-t TestArch` 280 OK, `-t BaseTests`
57 OK, `-t TestAssemblyWorkbench` 16 OK;
`tests/gui/python-settings-door.py`, two claims more, 7 PASS, 1 PASS and 6
FAIL on the staged binaries. The full suites were not run again after
these two commits (last: `427ffc8d28`). Not staged, not pushed.

Left of the entry after this step: step 3 (Fem, CAM, the Addon Manager;
done below) and step 4 (Help, OpenSCAD, ReverseEngineering, Tux, Material,
Test). A module with plain reads takes a definition file
(`freecad.params`); one with a table of its own registers from that.

**The list for the reporter,** nothing of it chosen or changed:

Defaults that disagree, or cannot be described by one value:
- L1 Assembly `BOMOnlyParts`: the task panel's `.ui` file has the box
  checked; the code reads the setting with False and sets the box from
  that. Described: False.
- L2 `Mod/Draft/DefaultPrintColor`: Draft's table says 255 (black); BIM's
  layers manager reads it with 0. Described: 255.
- L3 `Mod/NativeIFC/SingleDoc` ("Always lock new documents"): the page has
  it off; `ifc_import.py` reads it with True at line 88 and with False at
  line 144, `ifc_status.py` with nothing. Described: off, the page's.
- L4 `Mod/BIM/LibraryOnline`: its default is "on unless a parts library is
  installed", computed when the library panel opens. NOT described.
- L5 `Mod/BIM/BimViewWidth`, `BimViewHeight`: stored as numbers and read
  back with `GetBool` -- and crossed, the height from the width. NOT
  described.

Seen on the way, and left:
- F1 `BimProjectManager.py` stores `View/DefautShapeLineWidth` (so spelled;
  nothing reads it), and it and `BimSetup.py` store `Mod/Draft/dimsymbol`,
  `arrowsize` and `color`, which Draft's table does not have.
- F2 Four of Part's settings are described twice, by `Part::PartParams` and
  by `PartGui::PartParams` (`MeshDeviation`, `MeshAngularDeflection`,
  `MinimumDeviation`, `MinimumAngularDeflection`): the omni search lists
  each of them twice. Found by a probe with every module loaded: 1233
  entries, 1229 distinct.
- F3 `Mod/Draft/ScaleRelative` is stored by the Scale task panel and read
  by nothing.
- F4 The titles and documentation written here (about 250) are English
  only: nothing extracts them for translation. A page setting's title and
  tool tip are translated, through the page's own context.
- F5 To decide: Draft's and BIM's settings are listed after first use of
  either workbench, because of the 1.9 s above; Assembly's from the start.

**Answered, 2026-10-08,** in the reporter's words: "F2 remove
PartGui::PartParams duplicates. F5 lazy loading, continue step 3". L1 to
L5, F1, F3 and F4 are not answered.

- F5: stays as it is, Draft's and BIM's settings are described when their
  table loads.
- F2 (`48037fbd8c`): the two classes share the four definitions on purpose
  -- App reads them, Gui follows a change at once -- so both keep their
  accessors. The registry keeps ONE description of a path and entry:
  `ParamRegistry::add()` of a generated class leaves out an entry described
  already, as the run-time `add()` does. The first stands, here Part's,
  whose library loads first; PartGui's four are no longer listed. `Tests_run`
  `ParamRegistry*` 10/10 (1 new); the GUI test's new claim passes (one
  `MeshDeviation`, no path and entry twice with every module of the test
  loaded).

**Step 3, Fem, CAM and the Addon Manager** (`de7bd49797`), 2026-10-08: 115 settings,
listed from the start of a session (each is a plain file read by the
module's `Init.py`):

- Fem, 47: `Mod/Fem/FemPyParams.py`, a definition file. Fem's preference
  pages are C++ pages whose settings Python reads, so the file was drafted
  from the pages' `.ui` files -- title, tool tip, combo items, spin ranges
  (`gen_ui_defs.py` in the entry's script directory) -- and 17
  documentations written where a page has no tool tip. Two more are what the
  mesh preview panel was last left with.
- CAM, 27: `Mod/CAM/CAMPyParams.py`, a definition file. `Path/Preferences.py`
  is not a table but constants and functions, so the settings are written
  out, each with the default its function passes.
- The Addon Manager, 41: `addonmanager_params_registry.py` reads the
  module's own `addonmanager_preferences_defaults.json` -- the table -- and
  adds a title and a documentation to each row.

Each has a test that holds the described defaults to the readers'
(`femtest/app/test_settings.py`, `CAMTests/TestPathSettingsRegistry.py`,
`AddonManagerTest/app/test_params_registry.py`), and that the settings of
the module's generated class are all still listed beside them (8 of Fem's,
13 of CAM's).

Added to the list for the reporter:

- L6 CAM `PostProcessorShowEditor`: the page has the box unchecked;
  `Path/Preferences.py` reads it with True. Described: True.
- L7 The Addon Manager's `NoProxyCheck`, `SystemProxyCheck`,
  `UserProxyCheck`: its defaults file gives each an empty text; the code
  stores and reads them as switches, with True, False, False. Described as
  switches with those.
- L8 Fem `Ccx/AnalysisNumCPUs` and `Netgen/NumOfThreads`: the default is the
  number of cores of the machine (and `femtools/ccxtools.py` reads the first
  with 1). NOT described.
- F6 CHANGED, one line: `addonmanager_preferences_defaults.json` was not in
  the Addon Manager's CMake list, so neither the build tree nor the staged
  copy had it, though `addonmanager_freecad_interface.Preferences` reads it
  when first made. It is installed now, because the registration reads it.
  What the Addon Manager did without it was not looked at.
- F7 Fem's `MeshPreviewSettings.ui` names its spin box's entry
  `previewFactor`; the code reads and stores `previewMeshFactor`.
- F8 CAM keeps the place and size of the post processor's dialogues, and
  the versions its asset migration was offered for, in groups named at run
  time: not described. The Inspect window's place and size are kept as text.

**Step 4, the small rest** (`ff12279ee6`), 2026-10-08: 47 settings (49 since
the answer to L9, below), where the sizing had said about 48 -- Help 12, OpenSCAD 15, ReverseEngineering
11, Tux 5, Material 4, the Test module none. Each has a definition file
that its `Init.py` imports, so they are listed from the start of a session:

- Help, 12: `Mod/Help/HelpParams.py`. The ten of its preference page and
  where the help panel was last docked. The page shows two groups of radio
  buttons and each button is a setting of its own; `Help.py` does not read
  them as groups but takes the first that is on (wiki, Markdown, GitHub,
  custom; browser, dialog, else a tab), and the documentation of each says
  where it stands in that order.
- OpenSCAD, 15: `Mod/OpenSCAD/OpenSCADParams.py`. The eleven of its page,
  and four the count had not seen because the reads run over two lines:
  `fnForImport`, `meshmaxlength`, `tempmeshmaxpoints`,
  `usePlaceholderForUnsupported`, on no page.
- ReverseEngineering, 11: `Mod/ReverseEngineering/ReverseEngineeringParams.py`.
  What the Fit B-spline surface dialog was last left with: its widgets store
  them when it closes and read them when it opens, and no code names them.
  Title, default and range are the dialog's (`Size factor` 1.0: the `.ui`
  gives the spin box no value and a minimum of 1); the documentation is what
  `approxSurface()` does with each. Nothing holds these eleven defaults to
  the dialog: its `.ui` is compiled in, not installed.
- Tux, 5: `Mod/Tux/TuxParams.py`. Under `User parameter:Tux`, not under
  Preferences, so the omni search lists them as `/Tux/...`: the navigation
  indicator's `Enabled`, `Compact` and `Tooltip`, and the persistent tool
  bars' `Enabled` and `Deprecated`.
- Material, 4: `Mod/Material/MaterialPyParams.py`, what the card editor
  written in Python and its card list read, beside the 23 of the generated
  class.
- Test: nothing to describe. The keys its own code makes are scratch keys
  under `System parameter:Test`; everything else its files read is a setting
  of another module.

`Mod/Test/ModuleSettings.py` (`-t ModuleSettings`, in the list of
`Mod/Test/Init.py`): every setting of the five files is in the registry as
described, with a title and at most 400 characters; each described default
is the one every reader in the module's sources passes, and the test asks
that it found at least 10 such reads in Help, 12 in OpenSCAD, 5 in Tux and 4
in Material; Material's generated class still has its 23 rows. Checked that it can fail: with `MaterialEditorWidth` and
`exportConvexity` changed in the build tree's copies it reported the first.

Scored: `-t ModuleSettings` 4 OK (new), `-t BaseTests` 57 OK,
`-t TestMaterialsApp` 75 OK; `tests/gui/python-settings-door.py`, one claim
more, 10 PASS. `docs/OmniSearch.md`, section 3.1, names the definition
files. Not staged, not pushed.

Added to the list for the reporter:

- L9 Help `dockWidgetWidth`, `dockWidgetHeight`: stored as numbers, read
  back with `GetBool`, and crossed -- the height from the width's key. The
  same four lines as BIM's views manager (L5). NOT described.
- L10 OpenSCAD `useMaxFN`: the page says 16 and `importCSG.py` reads it with
  16 in four places; `prototype.py` reads it with nothing, so 0, which means
  no limit. Described: 16.
- L11 Material `Cards/SortByResources`: the page has the box checked;
  `MaterialEditor.py` reads it with False. Described: False.
- L12 Help `optionTab` is stored by the page and read by nothing -- the tab
  is what is left when neither of the other two is on. And `optionGithub`:
  the page has the button disabled ("currently not available"), the code
  honours the setting. Both described, each saying so.
- F9 OpenSCAD `meshmaxarea` and `meshlocallen` are read only by a branch
  switched off (`if False: # disabled due to issue 1292`): NOT described.
  `meshmaxlength` is the tessellation tolerance in the branch that runs;
  described as that.
- F10 CHANGED: Help and Tux had no `Init.py`, only an `InitGui.py`. Each has
  one now, holding the one import, so that their settings are listed without
  the GUI as well and `BaseTests` sees them.
- F11 Tux `PersistentToolbars/Deprecated` is a marker more than a setting:
  1 hands the kept tool bar places over to the main window at the next start
  and becomes 2, 2 means Tux leaves the tool bars alone, 0 makes Tux keep
  them itself as it used to. Described, as an integer saying that. The
  places themselves are in groups named after the workbenches: not
  described, as with F8.
- F12 Material `Cards/LegacyEditor` is named only in a commented-out line of
  `MaterialEditor.py`: not described.
- F4 again: the 47 titles and documentations are English only.

**Entry 41 is done with this step:** 601 settings listed that were not (13
+ 426 + 115 + 47). What is open is the list -- L1 to L12, F1, F3, F4, F6 to
F12 -- none of which blocks anything.

The full suites on `ff12279ee6`, the first full run since `427ffc8d28`
(`full41.cmd` in the entry's script directory): ctest 787 of 787 passing
(796 entries, 9 disabled, 1 skipped; 784 before, +3 from this entry);
`FreeCADCmd -t 0` 3411 tests, 2 failures, 50 skipped, 6 expected failures
(3385 before with the same two failures, +26 from this entry). The two are
`TestThickness.testCase5829ThicknessOnRotatedFillet` and
`testCase5829RectoVersoThicknessOnRotatedFillet`, red since the OCCT merge
of 2026-10-05 and not this entry's. `docs/Testing.md` has the counts.

**Answered, 2026-10-08,** in the reporter's words: "L9 check with upstream.
fix the read side if upstream also borken. L10 use 16. L11 check with
upstream. F9 drop. F10 elaborate. F11 drop. F12 drop." F9, F11 and F12 are
read as closed with nothing to do: the two dead keys and `LegacyEditor`
stay undescribed, Tux's marker stays described.

- L9 (`aa63b07cc8`): upstream has the same four lines (`upstream/main` at
  `b960974504`, 2026-10-01), so the read side is fixed here: width and
  height are read with `GetInt`, each from its own key, with the defaults
  the lines had (200 wide, 300 high). What the old read did, measured: a
  switch read with a default of 200 is True, so the panel was made 1 by 1
  -- with 520 and 410 stored a new floating help panel was 82 by 1, and is
  520 by 410 now (`help_panel_size.py` with the entry's scripts, run by
  hand before and after; not a registered test). Both keys are described
  now, so Help has 14 settings and the entry 603. BIM's views manager has
  the same lines (L5), in upstream too; L5 is not answered and was not
  touched.
- L10 (`aa63b07cc8`): `prototype.py` passes 16.
- L11, checked: upstream is the same on both sides -- its
  `DlgSettingsMaterial.ui` has the box checked, its `MaterialEditor.py`
  reads `SortByResources` with False. Nothing changed. What it means in
  use: the Python card editor sorts its list by name until Material's
  preference page has been saved once, and by resource after, because the
  page stores its checked box. Recommended, not done: read it with True,
  the page's.
- F10, elaborated. A module directory is initialised in two passes:
  `FreeCADInit.py` runs its `Init.py` in every session, `FreeCADGuiInit.py`
  its `InitGui.py` when there is a GUI. Help and Tux had only the second
  (upstream too). The definition file has to be imported from one of them;
  the other three modules of step 4, and Assembly, Fem and CAM before them,
  import it from `Init.py`, so the same was done here by adding the file,
  five lines with the one import. What it changes: the two modules'
  settings are listed in a session without GUI as well (`FreeCADCmd`), and
  `BaseTests.testModulesDefinitions` and `-t ModuleSettings`, which run in
  `FreeCADCmd`, see them. Nothing else of the module is loaded by it:
  `Help.py` and Tux's GUI files are imported by `InitGui.py` as before. The
  cost, measured by running each definition file again in a started
  session (best of five, `time_init.py`): Help's 4.4 ms, Tux's 1.5 ms; the
  eight definition files between 1.5 and 4.5 ms each. The first run, which
  also compiles the file, was not timed apart. The other way would be the
  import in `InitGui.py`: no new file, listed in GUI sessions only, and
  out of reach of the two tests. Help's sources and translation suffix are
  read without a GUI too (`Help.show()` prints the page there); Tux's five
  only mean something with one.

**The rest of the list, answered 2026-10-08.** Every open item was put to
the reporter again with a suggestion each, checked against `upstream/main`
(`b960974504`) where that could be done. The reporter: "My previous answer
of 'drop' is meant to not show them from omni search. Agree with your
suggestions". So "drop" means out of the registry, which F11 had not got,
and the suggestions are carried out (`6a2216d0f0`):

Readers made to say what their page says -- on a profile that never saved
the page the reader's default was what one got, and the page's after:
- L3 NativeIFC `SingleDoc`: the three reads pass False, the page's
  (`ifc_import.py` passed True in one place; upstream the same).
- L6 CAM `PostProcessorShowEditor`: read with False, the page's (upstream
  reads True against an unchecked box too). Described False now.
- L11 Material `Cards/SortByResources`: read with True, the page's.
  Described True now.
- L1 Assembly `BOMOnlyParts` is left: the code reads False and sets the
  box from it, so the `.ui`'s checked box never shows. Upstream the same.

Defects, each in upstream too:
- L5 BIM's views manager read its width and height as L9's panel did;
  read with `GetInt` now, each from its own key, and both described (200,
  300). NOT shown to change anything on screen: the manager is docked right
  after it is given the size, and was 278 by 265 before and after with 430
  and 380 stored (`bim_views_size.py`).
- F1 `BimProjectManager.py` read and stored `View/DefautShapeLineWidth`;
  it is `DefaultShapeLineWidth` now, the key the view reads. The three
  `Mod/Draft` keys it and `BimSetup.py` store are left.
- F7 Fem's `MeshPreviewSettings.ui` names its entry `previewMeshFactor`,
  as the code does.
- L2 was not what the list said. BIM's layers manager did not read
  Draft's `DefaultPrintColor` with another default: it read a
  `DefaultPrintColor` of the VIEW group, which nothing stores, so a new
  layer's print colour was black whatever Draft's setting said -- and 0 and
  255 are both black to that reader, so the number agreed on would have
  changed nothing. It reads Draft's setting now, with the line Draft's own
  layers manager has. This goes further than the words agreed; said to the
  reporter.

Out of the registry, so not shown by the omni search:
- F11 Tux `PersistentToolbars/Deprecated`. Tux has 4 settings.
- L12 Help `optionTab`, which nothing reads. `optionGithub` stays. Help
  has 13.
- L4 `LibraryOnline`, L8 Fem's two thread counts, F8 CAM's run-time groups,
  F9 and F12 were never in it and stay out.

Left as they are: L7 (the Addon Manager's three proxy switches, described
as switches), F3 (`ScaleRelative`, in Draft's own table), F4 (the titles
and documentation are English only), F6 (the Addon Manager's defaults file
is installed).

The entry lists 603 settings after this: Assembly 13, Draft and BIM 428,
Fem 47, CAM 27, the Addon Manager 41, Help 13, OpenSCAD 15,
ReverseEngineering 11, Tux 4, Material 4. Counted in `FreeCADCmd` with
Draft's table loaded (`count_ns.py`), all but one are as said; the Addon
Manager shows 40 under its own name where its file has 41 rows, which was
not looked into.

Scored on the tree of `6a2216d0f0`, both full suites: ctest 788 of 788 (one
more, entry 44's); `FreeCADCmd -t 0` 3411 tests, the two `TestThickness`
5829 failures and nothing else; `-t ModuleSettings` 4 OK. Nothing of the
entry is open with the reporter. Not staged, not pushed.

## 42. State keys through the generator, and listed -- FIXED, all three steps; Q1 to Q5 answered, Q6 with the reporter

Decided under entry 24 (C2), and the reporter, 2026-10-08: "Entry 42, list
those keys". Not started. Sized with the same run
(`entry42-candidates.tsv`).

About 300 keys that C++ reads have no definition (589 pairs by the
inventory, 289 of which are defined and only read through a group it did
not resolve). Read through once, by eye, they are three kinds, and only
the first is what C2 named:

- state, about 90: window and panel geometry and sizes, which panel is
  expanded or shown, the last directory, filter, tab and increment, the
  recent lists, first-run flags;
- settings entry 24 did not reach, about 110: the 3D mouse (about 40),
  the expression sandbox (11), DXF import and export as C++ reads it
  (16), the web site addresses (8), and small groups in Gui, Part,
  TechDraw, Material, MeshPart, Inspection, Raytracing;
- records under names the user makes, about 90: macro commands, the scene
  server's grants, clients and doors, custom shortcuts and tool bars, the
  workbench order. A key of these has no fixed place, so a definition of
  the generator's kind does not fit it.

The split is by reading the list, not by a rule; the first step of the
entry is to make it key by key.

The reporter, 2026-10-08: "42 leave the user-named ones, add those missed
ones to generator in the same entry." So: (1) the split key by key;
(2) the about 90 state keys through the generator, listed; (3) the about
110 missed settings in this entry too, group by group, the 3D mouse
first; the records under names the user makes stay out. After entry 41,
and not before the next session.

**Step 1, the split key by key,** 2026-10-08. Nothing in the tree changed.
Each of the 300 candidates was read at its sites (by a subagent of the
build session; `..\dl\handson\2026-10-08\entry41-42-sizing\`:
`entry42-split.tsv`, a row per path and key with type, the default of each
reader, kind, where it is set and a note; `entry42-split-summary.txt`).
Ten candidates were one key name in several groups and became 25 rows; 315
rows, 289 distinct pairs of path and key:

| kind | pairs | what |
|---|---|---|
| state | 94 | the overlay panels' 13, `General` 10, `View` 8, `MainWindow` 7, the 3D mouse's calibration 7, `PropertyView` 6, Sketcher 10, and small groups |
| setting | 114 | the 3D mouse's motion 24, the expression sandbox 13, the web addresses 8, the cube's face labels 6, gizmos 5, `PropertyView` 5, the scene share 5, and small groups; 58 of them are read and set nowhere |
| record | 44 | macro commands, share grants and doors, Material's modules and interfaces, printers, 3D mouse buttons, custom tool bars, per-widget geometry and history: stay out |
| dead | 23 | 5 of `OnlineHelp` whose command is never made, 5 of the cube inside an `#if 0`, 9 that are only comments, 4 written or read to no effect |
| defined after all | 14 | 2 by a definition file, 9 by Draft's table (the DXF page), 3 by the Addon Manager's |

So the sizing's "about 90" and "about 110" hold: 94 and 114.

Checked by the build session, not taken on trust: the live registry was
dumped with every module and Draft's table loaded (1818 rows,
`all-registry.tsv`; `check_split.py`) -- all 14 called defined are in it,
none of the 208 called state or setting is; and a sample of rows was read
against the code (the 3D mouse's `Calibrate`, the four `DockWindows/*/Enabled`,
the main window's `Geometry`, the two `MRU` lists, the share token,
`PreferCompactFormat`, `SavePicture`, `Workbenches/Disabled`).

Where state ends and setting begins is a judgement for about 25 keys the
program stores on the user's choice without a page (the overlay panels'
`AutoHide` and `Transparent`, `UserEditMode`, `SavePicture`, the headlight's
rotation, the scene share's port and door); the split follows the comments
the definition files already have ("kept by the program"). Both kinds go
through the generator, so the line decides nothing but the wording.

For the reporter, before anything is generated:

- Q1 28 of the 114 settings do not take a plain definition: 17 have no
  literal default (the cube's 6 face labels and 6 of the web addresses are
  translated texts, `IssuesPage` comes from the build's configuration, two
  gesture keys from Qt, `BitmapFill` is a resource path, `LogLevels/Default`
  is computed); 4 are developer switches (`WireJoiner` x2,
  `PyodideUnpinned`, `LogLevels/DebugDefault`); 2 are old keys read only as
  the fall-back of their successor (`DAGView/Enabled`,
  `TechDraw/HLR/UsePolygon`); 1 mirrors four radio buttons (`DxfImportMode`);
  4 are the DXF exporter's of Q2.
- Q2 FOUND, a defect, not fixed; it is entry 44 of the queue now: the C++ DXF exporter reads its four
  options (`maxsegmentlength`, `ExportPoints`, `DxfVersionOut`,
  `DiscretizeEllipses`) from `Mod/Draft` in its constructor, and
  `Import.writeDXFObject`/`writeDXFShape` then point it at `Mod/Import` and
  read again (`AppImportPy.cpp:486`, `520`), where nothing stores them: the
  DXF page's exporter settings do not reach it. Read in the code, not run.
- Q3 `SceneShare/Token` is a secret kept so that links handed out go on
  working: defined or not, its value should not show in a search.
- Q4 `Workbenches/Ordered` and `Disabled` have a fixed path and key and are
  set on a page, so the split has them as settings; the sizing had listed
  "the workbench order" with the records that stay out.
- Q5 The two recent lists are keys `MRU0`, `MRU1`, ... in a group that is
  cleared and rewritten: counted as state, but a definition per key does not
  fit them; only their length (`RecentFiles`) can be defined.
- Q6 Eight commands store the translated default they have just read (the
  web addresses), freezing it into `user.cfg`; the same write-back is in
  `AutoShowSelectionView`, three `DAGView` keys and four `DockWindows/*/Enabled`.
  Defaults that disagree between readers: `DockWindows/PropertyView/Enabled`,
  `MainWindow/Theme` ("" and "Classic"), `General/LastModule`.

Left of the entry: (2) the 94 state keys and (3) the 114 settings through
the generator, the 3D mouse first.

**Answered, 2026-10-08,** in the reporter's words: "Q1 what do you mean
don't take plain definition? Q2 fix. Q3 drop. Q4 Drop. Q5 drop."

- Q1: asked back, explained to the reporter, NOT ANSWERED YET. A definition
  is one line that gives a setting a name, a type and ONE default written
  into the file. 28 settings do not fit that: 17 whose default is worked
  out when the program runs (the cube's six face labels and six web
  addresses pass through the translator, `IssuesPage` is the build's, two
  gesture thresholds are Qt's, `BitmapFill` is a path under the resource
  directory, `LogLevels/Default` the console's); 2 old keys read only when
  their successor is not set; `DxfImportMode`, which mirrors four radio
  buttons; 4 developer switches, which are plain but perhaps not wanted in
  a list; and the 4 of Q2, which are gone with its fix. Proposed: define
  the 4 developer switches, leave the 20 out and list them.
- Q2: fixed as entry 44, below.
- Q3: `SceneShare/Token` stays out of the registry.
- Q4: `Workbenches/Ordered` and `Disabled` stay out.
- Q5: the two recent lists stay out; their length is defined.
- Q1, answered after the explanation, 2026-10-08: "Q1 agree." So the 4
  developer switches are defined, and the 20 stay out and are listed here
  when the settings are generated.
- Q6 was not among the answers. It asked for nothing yet; it comes up
  again with each group whose reader writes its default back.

So the field is 91 state keys (94 less the token and the two lists) and,
of the 114 settings, 88 (less the 2 of Q4, the 4 of Q2 and the 20 of Q1),
and the 2 of entry 44 (`ExportPoints`, `DxfVersionOut` in `Mod/Draft`).

**Steps 2 and 3, the definitions,** 2026-10-08, in four commits. 174 keys
are defined -- 89 settings and 85 state keys -- which is 213 rows of the
registry, because each of the 13 keys of an overlay panel is there for
the four panels. Two groups came first and whole, readers and all; the rest
went in three batches.

- The 3D mouse (`aa3e77137c`), 32: a new class `Gui::SpaceballParams` for
  `BaseApp/Spaceball/Motion`, which is not under Preferences -- the 23
  settings of the Spaceball Motion page, `Remapping`, the 7 the program
  stores when it calibrates, and `Model` of the group above. The page only
  shows its widgets while a device is present, so on a machine without one
  these could be seen nowhere. Every reader asks the class; the motion
  event read 31 keys from the group at each movement of the device. NOT
  RUN: there is no device here, the motion path is converted by reading.
- The expression sandbox (`dbadedb7ba`), 14: a new class
  `App::SandboxParams` for `Expression/Sandbox` and `Expression/Security`.
  Every reader asks the class but one: the sandbox host caches the four
  budgets inside the group's change notice, which the class may not have had
  yet, and takes its defaults from the class.
  `ExpressionWasmtimeRuntime.cpp` is not compiled on this machine; its two
  reads are converted by reading.
- Gui's small groups, 30, readers converted: the rest of the gizmos' group,
  the property view (5 settings, 6 state), the panel mirror, the selection
  view and the feature picker, the DAG view, the custom orientation of a
  new document's view, `MainWindow/ClearMenuBar`,
  `View/CursorCrosshairColor`, `DependencyGraph/GeoFeatureSubgraphs` (whose
  reader is App's and stays). Put right on the way: the property view did
  not follow a change of `AutoTransactionData`, which was missing from the
  list it watches.
- The rest of Gui, 68 keys (107 rows), DEFINED ONLY -- the readers are left
  on their groups: what the program keeps in `General` (10), `MainWindow`
  (7), `View` (8), `Macro` (1), `Document` (2), the combo view's two
  sizes, the Placement dialog's last method, the Transform panel's two
  increments, the two dock flags read by name, the 13 keys of each overlay
  panel; and the settings `RecentFiles`, `Websites/DonatePage`,
  `Paths/Graphviz`, the icon theme's three, the five `DockWindows/*/Enabled`,
  the Share dialog's five fields and its two one-time flags,
  `ActivateOverlay` and `CursorMargin`. They are in `GeneralParams`,
  `MainWindowParams`, `ViewParams`, `MacroParams`, App's `DocumentParams`
  and `MiscParams`.
- The modules, 30: Sketcher 9, Material 6, Mesh 3, Start 1 and TechDraw's
  `SectionLiveUpdate` are state, defined only, in their classes;
  TechDraw's `TileColor`, Part's `GridLinePattern` and the wire joiner's two
  ask their class; Part's `MaximumPatternOccurrences` is defined, App reads
  it. Inspection has no class: `Mod/Inspection/InspectionParams.py`, a
  definition file as ReverseEngineering's, for the two values its dialog
  keeps. The three of Draft's group that the C++ DXF code reads
  (`dxfUseDraftVisGroups`, `ExportPoints`, `DxfVersionOut`) are a new table
  `IMPORT_READ` of `draftutils/params_registry.py`.

"Defined only" is where this entry stops short of entry 24's way: a state
key has no default of its own -- the reader passes whatever its widget
shows -- and its reader was not touched. The value written in the
definition is what the reader finds on a fresh profile.

NOT defined, and why:
- the 20 of Q1: 17 without a literal default (the cube's 6 labels, 6 web
  addresses and `IssuesPage`, the 2 gesture keys, `BitmapFill`,
  `LogLevels/Default`), `DAGView/Enabled` and `HLR/UsePolygon` (old keys
  read as a fall-back), `DxfImportMode`;
- `LogLevels/DebugDefault`, one of Q1's four developer switches, which the
  reporter agreed to define: it turned out to have no literal default
  either (the console's level, written when missing) and to exist in debug
  builds only. The other three are defined. SAID TO THE REPORTER;
- the share token (Q3), `Workbenches/Ordered` and `Disabled` (Q4), the two
  recent lists (Q5);
- the 4 of `Oculus`, whose file no preset compiles;
- Sketcher's `SelectedConstraintFilters` (its default is a bit per filter
  entry) and `GridSize/Hist0` (an old key read as a fall-back).

Q6, as met: the readers that store what they have just read are left doing
so (`AutoShowSelectionView`, the DAG view's three, `DonatePage`, the dock
flags). Where readers disagree the definition says what a fresh profile
gets and the readers are unchanged: `DockWindows/PropertyView/Enabled`
True, `MainWindow/Theme` empty, `General/LastModule` empty.

`tests/gui/state-and-missed-settings.py`
(`GuiStateAndMissedSettings_tests_run`), claims per group: listed by
"/param" with their groups outside Preferences; Security/Enforce and the
property view's `HideHeader` followed at once; what was to stay out is not
in the registry; nothing is described twice. `preferences-ok-keeps-defaults.py`
is told of one key: Material's count of recent materials, which closing a
page's material chooser raises.

Scored: `state-and-missed-settings.py` 21 PASS, `preferences-ok-keeps-defaults.py`
7 PASS, `module-settings.py` 10 PASS, `python-settings-door.py` 10 PASS (the
GUI tests do not register with ctest on this machine and were run by
hand); `-t ModuleSettings` 4 OK, `-t TestDraft` 91 OK, `-t BaseTests` 57 OK.
Both full suites, three times on the way and last on the tree before the
final two fixes -- a copy rule for Inspection's file and the note to the
preferences test: ctest 788 of 788, `FreeCADCmd -t 0` 3411 tests with the
two `TestThickness` 5829 failures and nothing else. Not staged, not pushed.

## 44. The DXF page's exporter settings do not reach the C++ DXF exporter -- FIXED `813d0250f9`, not staged

Found under entry 42 (Q2); the reporter, 2026-10-08: "Q2 fix."

`Import.writeDXFShape` and `writeDXFObject` pointed the writer at
`Preferences/Mod/Import` for its options unless given an option source,
and Draft's DXF export calls them without one. Nothing stores the
exporter's options there; the DXF preference page stores "Treat ellipses
and splines as polylines" (`DiscretizeEllipses`) and the segment length
(`maxsegmentlength`) in `Preferences/Mod/Draft`. The default is `Mod/Draft`
now, as it is for `readDXF` here and for every DXF function of upstream
(`upstream/main` at `b960974504`), so the line was the fork's own.

Measured with an ellipse written by `Import.writeDXFShape`
(`dxf_export_options.py` with entry 41's scripts, `FreeCADCmd`, a profile
of its own): before, one `ELLIPSE` whatever the page said; after, an
`ELLIPSE` with the option off, an `LWPOLYLINE` of 24 points with it on at a
segment length of 5, and of 198 points at 0.5.

Test: `ImportErrorsTest.writeDXFShapeTakesTheOptionsOfTheDxfPage`
(`Import_tests_run`, 6 of 6). The full suites were not run again after it.

Left: the exporter also reads `ExportPoints` and `DxfVersionOut` from the
same group, which no page shows and nothing describes; they are two of
entry 42's settings now, in `Mod/Draft`. Draft's own export passes the
version itself (14, or 12 without splines), so `DxfVersionOut` only counts
for a caller that passes none.

## 45. A spreadsheet's view provider makes its view when it is only asked for it -- FIXED `c7fdcf3220`, not staged

Handed over from another session with a design the reporter had agreed
to there; the reporter, 2026-10-08: "dwin shall build it once he finishes
what he's doing", and on the branch: "yes, with entry 27". Built on
PartDesignPort, on top of entry 27's `fa2ada985c`.

**Tests first.** `tests/gui/sheet-view-on-request.py`, a box and two
sheets, scored on the tree before the change: 14 PASS, 3 FAIL of 17.
- One click on a sheet in the tree selected it AND opened its view.
- `Std_ViewCellShowObject`, a 3D view active beside an open sheet and the
  other sheet selected: the selected sheet took the open sheet's cell and
  that view was closed. The other session's third finding; it happens
  here too.
- As wanted already: a double click opens; a click on an open sheet brings
  its view to the front; `setEdit` on a sheet that is not open opens it and
  leaves it active; a pick from a cell's menu lands in that cell and
  leaves the other sheet alone; a saved document comes back with its sheet
  in its cell. Entry 27 had taken the menu's listing off the view
  providers, so the first finding of the other session does not happen
  here.

**The change.**
- `ViewProviderSheet::getMDIView()` answers with its view or nothing. The
  contract is written at `ViewProvider::getMDIView()`: a question, never a
  creation.
- `ViewProviderDocumentObject::getOrCreateMDIView()`, new and virtual: the
  view for a caller that is going to host it, made if there is none. The
  default asks, calls `show()` and asks again; the sheet's opens its view.
  Its users: the cell menu's pick (`ViewArea.cpp`), `Std_ViewCellShowObject`
  and the layout that comes back with a document (`Document.cpp`). The
  lookup for a maximized cell stays a question, as do the layout's own
  naming of its views and the expression editor.
- The sheet's `doubleClicked`, `setEdit` and "Show spreadsheet" are
  unchanged.
- NOT as handed over: the design had the sheet make its view BARE, for the
  caller to place. On this branch a view opened for a cell is placed into
  that cell by entry 27's `ViewPlacement::IntoCell`, which the three
  callers already stood in, so the sheet's request opens the view the way
  it always did and the scope puts it where it was asked for. The layout
  that comes back has no such scope: the view is placed by the policy and
  the layout then takes it into its cell, as it does for a TechDraw page.
  SAID TO THE REPORTER.
- The second failure was not the question at all. With the first change in,
  the selected sheet had no view and a 3D view was the active one when the
  command ran, and it still went into the open sheet's cell.
  `Std_ViewCellShowObject` took the view area's OWN active cell, the one
  last clicked into or filled; `MainWindow::setActiveWindow()` -- the
  tree's sync view, a script -- changes the active view and does not move
  that cell. The command takes the cell of the active view now. The two
  can still differ elsewhere; not looked at.

Scored: `sheet-view-on-request.py` 18 PASS (one check added on the way),
`view-cell-menu.py` 15 PASS, `spreadsheet-select-all.py` 2 PASS, run by
hand as the GUI tests do not register with ctest here. Both full suites on
the tree before the command's line: ctest 788 of 788, `FreeCADCmd -t 0`
3411 tests with the two `TestThickness` 5829 failures and nothing else.

Not covered: the expression editor and the link dialog ask the same
question (`ExpressionEditorView.cpp`, about line 905; no `getMDIView()` by
that spelling in `DlgPropertyLink.cpp` here). They have no claim in the
test; they are right by the question being one now. The other session's
`GuiSheetViewReopen` and `GuiTaskPanelKeptSheetView` are not in this
tree; the reopen claim here stands in for the first.
