# Thickness in the OCCT fork, before and after

FreeCAD's Thickness -- `Part::Thickness`, `PartDesign::Thickness`,
`Shape.makeThickness()` -- hollows a solid by removing faces and giving the
rest a skin. Underneath it is OCCT's `BRepOffsetAPI_MakeThickSolid`, and the
fork (`~/works/sw/occt`, branch `LinkVibe-801`) carries a chain of changes to
it so that a *concave* face can be removed, which upstream does not support
(realthunder/OCCT#1-#4). From 2026-09-30 to 2026-10-01 that chain was
measured against upstream for the first time and the thickness failures were
chased down, one cause at a time. This page shows what each fix did, in
pictures. The build log -- every cause, what was tried, the hand-worked
volumes, the gates -- is docs/TransactionLog.md sec 27.88 to 27.96.

Where things are:

- The fork's suite: `tests/thickness/run_tests.py` (`FreeCADCmd
  tests/thickness/run_tests.py`; PASS 60, XFAIL 2) and its `README.md`, the
  case-by-case reference.
- The pictures: `tests/thickness/models/pictures/<case>.png` in the fork, one
  per case of the suite that a fix turned from failing to passing; this page
  links them by the sibling-checkout path (`../../occt/...`), as the two
  repositories sit side by side on the dev box.
- The tools that made them: `tests/thickness/pictures/` in the fork
  (`make_pictures.sh`, see "Making the pictures" below).
- FreeCAD's own cases: `parttests.regression_tests` in `src/Mod/Part`, one
  test per step (`test_thickness_*`).

## Reading a picture

Each picture is one case: a solid, the face removed (`FaceN`, 1-based as in
`shape.Faces`), the direction (`+1` outward, `-1` inward, thickness 1), the
join (Arc or Intersection) and whether intersection mode is on; the expected
volume, or "a valid solid" where no reference exists; and the section of
TransactionLog.md that fixed it.

Three columns:

- **upstream OCCT 8.0.1** -- upstream's eleven files of the fix chain at the
  fork's base (`91be8c4c71`), compiled into scratch `TKBool`/`TKOffset`
  libraries and preloaded, the rest of the fork as it is.
- **fork before** -- the fork's `TKBool`/`TKOffset` sources at the commit just
  before the fix (named in the heading), the same way.
- **fork after** -- the fork now.

Two rows: the result seen from the removed face's side, and the result cut
open -- the half towards the camera cut away by a plane through the removed
face's centre, the cut faces orange, so the walls show their thickness.

Under each column heading: "valid, volume V" in green, or what was wrong in
red. "Unhollowed" is a result whose volume is the input's: the opening was
never cut, though the solid may be valid. "Inside out" is a negative volume;
such a solid is drawn black (its back faces), and its cut is done by a clip
plane with no caps, as is any cut the boolean refuses ("clipped"). Where the
thickness threw, the input is drawn greyed. Red edges are edges of the result
not used exactly once each way round by its faces (free, doubled, or turned);
red faces fail `isValid()`; faces of no area are left out, and the picture
says how many.

Upstream's column is one run of many: upstream visits offsets in hash order
and its wrong results vary from run to run (sec 27.88). Of the 46, two came
out different on a second run (`tshape_bar_top_out`, `filletbox_end_out`),
wrong both times. The fork's columns are the same every run.

A result can be right only when it is a valid solid with one closed shell and
the expected volume; the volumes are upstream's where upstream is right, and
otherwise worked out by hand (TransactionLog.md has each derivation).

## How it stood, and how it stands

The sweep behind all of this (TransactionLog.md sec 27.89): 712 thickness runs
-- 16 solids, every face, +/-1, intersection off and on, the Arc and the
Intersection join -- against both builds. A run counts as right only as
above, with the volume plausible for a skin where no reference exists.

| After | Fork worse than upstream | Fork better | Fork right, Arc join (int. off / on) | Fork right, Intersection join (off / on) | Upstream right (four modes) |
|---|---|---|---|---|---|
| start (sec 27.89) | 144 | 51 | | | |
| sec 27.89 | 26 | 53 | | | |
| sec 27.90 | 9 | 59 | 123 / - | | 101 (Arc, off) |
| sec 27.91 | 0 | 78 | 135 / 135 | 129 / 113 | 106 / 107 / 110 / 111 |
| sec 27.92 | 0 | 110 | 135 / 135 | 137 / 137 | 106 / 107 / 110 / 111 |
| sec 27.93 | 0 | 122 | 141 / 141 | 137 / 137 | 106 / 107 / 110 / 111 |
| sec 27.95 | 0 | 138 | 149 / 149 | 137 / 137 | 106 / 107 / 110 / 111 |
| sec 27.96 | 0 | 162 | 149 / 149 | 149 / 149 | 106 / 107 / 110 / 111 |

The "right" counts are of the 150 runs a mode that are in scope; sec 27.90
set the scope (a face whose removal leaves the shell in pieces is out) and
sec 27.91 rebuilt the pocketed box (walls 2.5, not twice the thickness), so
counts before sec 27.91 do not compare with those after it.

Of the 46 pictured cases, 21 are ones upstream gets right and the fork had
broken -- the chain's casualties (sec 27.89, 27.90, part of 27.91). The other
25 fail upstream too: the fork now does better than upstream there.

What is still failing (upstream too): the holed cone's top inward, in every
mode -- the wall is thinner than twice the thickness and no hollow result
exists -- and, in the suite, `cyl_side_out/in`, a cylinder with its only
seam-carrying lateral face removed (`StdFail_NotDone`).

## The fixes

### Sec 27.88: the same result every run

Not pictured: no single picture shows it. With intersection on and the Arc
join, `BuildOffsetByArc` visited its offsets in hash order -- a shape's hash
is its TShape's address -- and the result depended on the order: up to four
different results in eight runs of one input. The offsets are now taken in
the shape's topological order. The suite's `arc_inter_*_same_every_run` cases
guard it.

### Sec 27.89: the chain's own regressions

Upstream passes every case here; the fork, before, broke all of them.

*A closed edge found twice* (`BRepAlgo_Loop::FindLoop`). On a periodic face
the seam wire replaced the closed-edge wires found before it, but the search
found the other closed edge again afterwards and kept it as a wire of its
own: the inner wall had wires [1, 3, 1] and the floor went missing -- the
open shell, seen as the black disc where the floor should be.

![cyl_bottom_in](../../occt/tests/thickness/models/pictures/cyl_bottom_in.png)
![hole_bottom_in](../../occt/tests/thickness/models/pictures/hole_bottom_in.png)

*One seam wire per face* (`FindLoop`). A removed cylinder leaves a wall at
each end, each closed by its own piece of the seam; the loop allowed one seam
wire per face, so the other wall was built from two bare circles -- the face
drawn red, which fails `isValid()`.

![hole_outer_out](../../occt/tests/thickness/models/pictures/hole_outer_out.png)
![hole_outer_in](../../occt/tests/thickness/models/pictures/hole_outer_in.png)
![hole_inner_out](../../occt/tests/thickness/models/pictures/hole_inner_out.png)
![hole_inner_in](../../occt/tests/thickness/models/pictures/hole_inner_in.png)
![boxhole_hole_out](../../occt/tests/thickness/models/pictures/boxhole_hole_out.png)
![boxhole_hole_in](../../occt/tests/thickness/models/pictures/boxhole_hole_in.png)

*A const edge losing its orientation* (`BRepAlgo_Loop::Perform`). The
concave-face pass replaced the const edges with FORWARD copies, and a seam
wire closed on two circles running the same way (inward); outward, the
re-found closed edge of the first cause.

![pocket_bottom_out](../../occt/tests/thickness/models/pictures/pocket_bottom_out.png)
![pocket_bottom_in](../../occt/tests/thickness/models/pictures/pocket_bottom_in.png)

*The offset of an ellipse stretched past its ends*
(`BRepOffset_Inter3d.cxx`, `ExtentEdge`). The offset of an ellipse is a
closed B-spline that is not periodic, and the context extension stretched it
100 lengths past its ends, evaluated there at 1e34: the bottom came back
unhollowed, the top as two shells with a face of no area (outward, of
negative volume). A closed edge on a
curve that is not periodic is no longer extended.

![ellipse_bottom_out](../../occt/tests/thickness/models/pictures/ellipse_bottom_out.png)
![ellipse_bottom_in](../../occt/tests/thickness/models/pictures/ellipse_bottom_in.png)
![ellipse_top_out](../../occt/tests/thickness/models/pictures/ellipse_top_out.png)
![ellipse_top_in](../../occt/tests/thickness/models/pictures/ellipse_top_in.png)

Not pictured: a crash in `BuildSplitsOfTrimmedFaces` (upstream code, reached
only with the fork's edges), now guarded; the suite's
`pocket_inter_join_no_crash`.

### Sec 27.90: past concave corners, and a concave face removed

*The corner piece of an arc face* (`BRepAlgo_Loop`). Outward with the Arc
join past a concave corner, the arc face along one edge is cut by the arc of
the next; the fork's loop keeps every piece of a cut edge (it needs them for
a concave removed face), and the piece beyond the cut, closed by the arc's
own end, came out as a face of its own -- the red sliver at the corner. A
wire through a piece beyond the edge's own span now loses to one that stays
inside. Upstream is right on the L-box and the T; on the pocket box it
returns the input, invalid.

![lbox_arm_end_out](../../occt/tests/thickness/models/pictures/lbox_arm_end_out.png)
![tshape_arm_end_out](../../occt/tests/thickness/models/pictures/tshape_arm_end_out.png)
![pocketbox_wall_out](../../occt/tests/thickness/models/pictures/pocketbox_wall_out.png)

*A neighbour skipped before its edge was renewed*
(`BRepOffset_MakeLoops::BuildFaces`) and *a stretched edge lying on another*
(`FindLoop`). The T's bar top beside the post -- a concave face -- removed,
outward: a corner sphere kept an old edge, and a piece of the removed face's
stretched edge lay on an arc face's tangent line. Upstream returns it inside
out, with two shells; the fork threw.

![tshape_bar_top_out](../../occt/tests/thickness/models/pictures/tshape_bar_top_out.png)

### Sec 27.91: inward past a concave top, and intersection mode

*Where the stretched removed face crosses an edge* (`BRepAlgo_Loop::Perform`).
The T's bar top removed, inward: the stretched removed face crossed the far
end wall's inner edge above the bar's inner top, and that crossing was taken
for the edge's own end; the corner above the bar's inner arc came out as a
face of its own and the build failed. Upstream returns the input unhollowed.

![tshape_bar_top_in](../../occt/tests/thickness/models/pictures/tshape_bar_top_in.png)

*An edge two faces share, trimmed twice* (`BRepOffset_MakeOffset.cxx`,
`TrimEdges`). The guard meant to trim each new edge once, an indexed map's
`Add()`, is never 0; with intersection on and the Intersection join the
second pass cut a section short, and the fork threw where upstream is right.

![lbox_top_inter_join_out](../../occt/tests/thickness/models/pictures/lbox_top_inter_join_out.png)
![lbox_top_inter_join_in](../../occt/tests/thickness/models/pictures/lbox_top_inter_join_in.png)
![tshape_back_inter_join_out](../../occt/tests/thickness/models/pictures/tshape_back_inter_join_out.png)
![tshape_back_inter_join_in](../../occt/tests/thickness/models/pictures/tshape_back_inter_join_in.png)

*A wire running once round a periodic face* (`FindLoop`). A cone with a
through hole, its bottom removed, inward, intersection on: a circle beyond
the seam's span came out as a face of no area, a second shell (the circle
drawn above the hole in the cut). Upstream is right.

![conehole_bottom_inter_in](../../occt/tests/thickness/models/pictures/conehole_bottom_inter_in.png)

### Sec 27.92: the Intersection join -- inside out, a blind floor, concave faces

Upstream fails every case here.

*A thick solid inside out* (`MakeThickSolid`). The quilt orients a shell by
the faces it meets first, so which way the result faced depended on the order
the shells met: valid solids of negative volume, drawn black. The solid is
now oriented by classification (`BRepLib::OrientClosedSolid`).

![tshape_bar_top_right_join_in](../../occt/tests/thickness/models/pictures/tshape_bar_top_right_join_in.png)
![pocket_floor_join_out](../../occt/tests/thickness/models/pictures/pocket_floor_join_out.png)
![pocket_floor_join_in](../../occt/tests/thickness/models/pictures/pocket_floor_join_in.png)

*A blind floor's section the wrong way round* (`ContextIntByInt`). A blind
hole's floor meets the wall at a concave edge, and the section on the wall's
offset came out reversed: a band closed on two circles running the same way.

![blindhole_floor_join_out](../../occt/tests/thickness/models/pictures/blindhole_floor_join_out.png)
![blindhole_floor_join_in](../../occt/tests/thickness/models/pictures/blindhole_floor_join_in.png)

*Concave removed faces with intersection on* (`MakeOffsetShape`). The splits
of the offset faces do not cut a neighbour's section where the rim needs it;
a shape whose removed face meets a neighbour at a concave edge is now built
as with intersection off, which gets all of these right.

![lbox_notch_wall_inter_join_out](../../occt/tests/thickness/models/pictures/lbox_notch_wall_inter_join_out.png)
![tshape_post_wall_inter_join_in](../../occt/tests/thickness/models/pictures/tshape_post_wall_inter_join_in.png)
![pocket_wall_inter_join_in](../../occt/tests/thickness/models/pictures/pocket_wall_inter_join_in.png)

### Sec 27.93: a removed face tangent to its neighbours

A box with its vertical edges filleted, an end or a side face removed. The
fillets' offsets run parallel to the removed face: inward they never meet it
(unhollowed), outward they meet it far round the cylinder -- a lip in the
opening (the side face: a valid solid, but 354.34 where 388.82 is right) or a
failure. A tube round the tangent edge now closes the gap, turning into the
removed face, with an eighth of a sphere at each corner outward. Upstream
fails all four.

![filletbox_end_in](../../occt/tests/thickness/models/pictures/filletbox_end_in.png)
![filletbox_side_in](../../occt/tests/thickness/models/pictures/filletbox_side_in.png)
![filletbox_end_out](../../occt/tests/thickness/models/pictures/filletbox_end_out.png)
![filletbox_side_out](../../occt/tests/thickness/models/pictures/filletbox_side_out.png)

### Sec 27.94: the loop's minimal wires by angle

Not pictured: FreeCAD's WireJoiner angle walk ported into `BRepAlgo_Loop`.
It changed no result (the walk and the old search were checked against each
other on every face of the suite and the sweep); it is what sec 27.95 uses on
a curved face.

### Sec 27.95: a fillet removed

The same box with a fillet removed -- a curved face tangent to the planes
beside it. Outward it threw; inward it came back valid at 647.56, more than
the input (459.40). The tubes
and corners are built on the fillet's cylinder; the loop on that periodic
surface, whose edges all lie within one quarter turn, walks the angles as on
a plane; and a piece of the floor's offset left hanging on the shell is
dropped. Upstream fails both. The cut is in plan, to show the opening.

![filletbox_fillet_in](../../occt/tests/thickness/models/pictures/filletbox_fillet_in.png)
![filletbox_fillet_out](../../occt/tests/thickness/models/pictures/filletbox_fillet_out.png)

### Sec 27.96: the Intersection join at a tangent edge

The Intersection join builds no tubes: the neighbour's offset ran round its
cylinder into a lip (outward) or never met the removed face (inward,
unhollowed; a fillet threw). The gap is now closed by the tube's sharp
counterpart, the user's choice of a square corner: a strip of the
neighbour's tangent plane a thickness into the removed face, offset with it,
and a wall square to the removed face at its far edge, cubes at the corners.
Upstream fails all eight.

![filletbox_end_join_in](../../occt/tests/thickness/models/pictures/filletbox_end_join_in.png)
![filletbox_end_join_out](../../occt/tests/thickness/models/pictures/filletbox_end_join_out.png)
![filletbox_side_join_in](../../occt/tests/thickness/models/pictures/filletbox_side_join_in.png)
![filletbox_side_join_out](../../occt/tests/thickness/models/pictures/filletbox_side_join_out.png)
![filletbox_fillet_join_in](../../occt/tests/thickness/models/pictures/filletbox_fillet_join_in.png)
![filletbox_fillet_join_out](../../occt/tests/thickness/models/pictures/filletbox_fillet_join_out.png)
![filletbox25_fillet_join_in](../../occt/tests/thickness/models/pictures/filletbox25_fillet_join_in.png)
![filletbox25_fillet_join_out](../../occt/tests/thickness/models/pictures/filletbox25_fillet_join_out.png)

## The captured models

The suite's four document cases (realthunder/OCCT#1-#4: an elliptic pad, a
loft, a pad, a revolution) are older fixes, made to the chain on 7.7.2 and
ported. Upstream 8.0.1 gets the three thickness models right too, so they
have no before-and-after here.

## Making the pictures

`tests/thickness/pictures/make_pictures.sh` in the fork does it all; about
ten minutes on the dev box, most of it building libraries:

1. `mkold.sh` builds scratch `TKBool`/`TKOffset` libraries -- upstream's
   eleven chain files at `91be8c4c71`, and the fork's sources at each
   stage's "before" commit (`STAGES` in `cases.py`) -- from the build tree's
   own compile commands (`oldbuild.py`).
2. `compute.py` runs every case of `cases.py` under `FreeCADCmd`, once per
   library set (preloaded with `LD_PRELOAD`; the FreeCAD modules carry an
   RPATH, so `LD_LIBRARY_PATH` does not reach them) and once on the fork as
   built, keeping each result as a `.brep` and a `results.json`.
3. `mkjobs.py` and `render.py` draw the panels in the FreeCAD GUI under
   `xvfb-run`; `compose.py` lays them out (Pillow, from the FreeCAD conda
   env).

A case added to `cases.py` with its stage gets its picture on the next run.
The renderer turns the transaction log off: while drawing, the log's worker
crashed once serialising a shape the viewer was meshing (TransactionLog.md
sec 27.97).
