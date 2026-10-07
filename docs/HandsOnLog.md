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

Evidence that does not belong in the repository is under
`..\dl\handson\<date>\`, as before.

| # | State | In one line |
|---|---|---|
| 15 | STAGED `1047cc0647`; one question for the reporter | a refine wrote into the feature underneath. Left: `Pocket040` is 12 mm where the file has 13 -- its negative `Fit` grew in the old build, on one oddly made face |
| 17 | STAGED `3c8cd63032`; what it uncovers is a question for the reporter | the two sketches refer to edges of a binder that moved with another binder; found again now. Then `Pad033` loses its profile, because the sketch really changes |
| 19 | STAGED `805b5afb25` | every place the audit listed that runs at load, recompute or paint, the three wrong results, and the writer of -1; what is left is listed |
| 22 | STAGED `5aedd5cf83` | `/word` is an object query; the beginning of a keyword lists modes and objects |
| 23 | STAGED `c7a27b5a85`, and `08b8f009aa`; the defaults FIXED `02cab053df`, not staged | 574 settings: 221 had no documentation, 94 ran past 400 characters; all have a short text now, and a test keeps it so. The defaults: OK on a fresh profile changed 23 settings and stored 2 under a wrong type -- 14 of them a spin box clamping its default to 99, which the reporter's own profile carries |
| 24 | STARTED: the method, the inventory (1734 pairs of group and key, 570 defined), three groups done -- the report view's 12 keys `a6ed59e824`, Part's measurements' 7 `014dc501f8`, the General group's 29 `e0a8a17e23` -- and about 200 more keys read; not staged | three things for the reporter to decide are in the entry |
| 26 | FIXED `175ffce199`, not staged | the first OK of a profile held the program 11 to 15 s on the reporter's configuration with `scanner.FCStd` open: 780 keys stored for the first time and taken for changes -- stylesheet set again 4.2 s, every Part view provider re-meshed 3.2 s, language activated again 2 s. 0.9 s now |
| 27 | STAGED `fa2ada985c` | the cell menu made a spreadsheet view by asking for it, listed a page's views, and a pick was placed by the general policy |
| 31 | FIXED `47b5e72c79`, not staged | "Go to end" is on for a profile that never stored it |
| 32 | FIXED `b960092ea5`, not staged | the menus styled see-through are single objects shared between a pop-up over the 3D view and the main menu; the blue is the palette's bright text, the desktop's accent. No sheet chosen is an ordinary menu now |
| 33 | FIXED `ef4df215b5` (cycles `35a3bd898`), not staged | the path tracer's CUDA probe ran `cmd.exe /c where nvcc` through `popen` at the first 3D view; it searches the PATH without a shell now |
| 34 | FIXED `3d7b4c30fd`, not staged | Dark and Light store TechDraw's preselection colour, the blue of the 3D view's |
| 38 | FIXED `bb31f8820b`, not staged | the omni search's first bring-up made the icon of every command before showing the box: 1.27 s on the reporter's configuration, 0.22 s now |
| 39 | FIXED `c7d115e576`, not staged | MSAA has not reached any view since 2026-09-07: the scene depth was built readable at every sample count and bgfx refuses that framebuffer. Write-only under MSAA now; a test asks the view its sample count |

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

## 24. Every setting behind a generated class -- STARTED, fifteen groups done

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
