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
| 23 | STAGED `c7a27b5a85`, and `08b8f009aa` | 574 settings: 221 had no documentation, 94 ran past 400 characters; all have a short text now, and a test keeps it so. What the audit turned up besides is listed |
| 26 | FOUND in part, nothing changed | an unchanged write of the renderer `Type` reloads every Part view provider: 1.07 s on `scanner.FCStd`; the anti-aliasing change itself 0.15 s. OK in the dialog not measured yet |
| 27 | STAGED `fa2ada985c` | the cell menu made a spreadsheet view by asking for it, listed a page's views, and a pick was placed by the general policy |

**The reporter, 2026-10-07 14:20, on what is open** (said to the build
session; the queue has the reporter's own entries):
- entries 15 and 17: "skip entry 15 and 17 for now" -- their open questions
  rest;
- entry 23: "yes fixed the defaults, leave the unused ones" -- the defaults
  that disagree between a definition and its preference page are to be made
  to agree; the settings nothing reads stay. Not done yet;
- entry 26: "probably not the view provider, because the delay I experience
  is longer. most likely related to stylesheet re-apply" -- so the second
  measured on `scanner.FCStd` is not the halt; what OK does to the
  stylesheets is where to look (entry 30's freeze is the same family).

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

## 26. A long halt after enabling MSAA and pressing OK -- FOUND in part

Nothing changed. What was asked: is the halt the update of every view
provider that OK sets off, or the anti-aliasing change itself?

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

**Next:** time OK in the preferences dialog itself on the reporter's kind of
session; make `InstancingGateObserver` act on a changed value only; look for
the other old-style observers that do work on an unchanged write.

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
