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

Stages since the split: none yet. The copy under test is still the one of
2026-10-07 10:37 (`7e94bff8d0`).

Evidence that does not belong in the repository is under
`..\dl\handson\<date>\`, as before.

| # | State | In one line |
|---|---|---|
| 15 | FIXED `1047cc0647`; one question for the reporter | a refine wrote into the feature underneath. Left: `Pocket040` is 12 mm where the file has 13 -- its negative `Fit` grew in the old build, on one oddly made face |
| 17 | FIXED `3c8cd63032`; what it uncovers is a question for the reporter | the two sketches refer to edges of a binder that moved with another binder; found again now. Then `Pad033` loses its profile, because the sketch really changes |

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
TestPartDesignApp 347 with the two known `TestThickness` 5829 failures, the
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
