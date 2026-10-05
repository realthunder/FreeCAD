# The new draft -- a draft that can change topology

Status: design, with a Python prototype (section 5) that the measurements
below come from. Nothing in OCCT or FreeCAD implements it yet.

Related: `PartDesign::Draft::Method` (fcad `2ed87066ea`), the fork's draft
fixes and suite (`occt/tests/fork/draft/README.md`), the draft pictures
(`occt/tests/fork/draft/models/Draft.md`).

## 1. What is asked

A PartDesign Draft turns faces about their line with a neutral plane, so
that each makes the given angle with the pull direction. The draft OCCT
offers, `BRepOffsetAPI_DraftAngle`, refuses many drafts a user would expect
to work. The fork made it refuse instead of handing back invalid solids
(2026-10-04), and gave PartDesign's Draft a `Method` property (2026-10-05):

- **Classic**: today's draft, nothing else.
- **New**: refine the base (unless its feature refines already), then draft.
  This document replaces what New does.
- **Auto** (default): Classic; if it fails, New.

Agreed then (2026-10-04/05): the real new draft is a draft that can change
topology -- a Parasolid-style local operation or a boolean rebuild --
designed here first, then built, behind `Method = New` and Auto's fallback.
Error reporting is not done on its own: it is folded into this work, so
that what the new draft still cannot do is said precisely (an angle too
steep for the face, a face that does not meet its neighbours, ...).

## 2. Why the classic draft refuses

`Draft_Modification` is a `BRepTools_Modification`: it gives each face a
new surface, each edge a new curve and each vertex a new point, and keeps
the topology -- every face, edge and vertex of the input is in the result,
connected as before. A draft whose true result has a different topology
cannot be made that way:

- a drafted face swept past the edge of a face beside it: the slot wall
  that breaks through the block's front (`slot_wall_a15`), a narrow wall
  at 5 deg;
- a face that touches the drafted face at a vertex only, which the
  drafted face's corner leaves (`notch_bevel_ledge_*`): the result needs a
  new edge there;
- a face that would shrink to nothing (`notch_ledge_a45`), or an edge;
- a wall split in coplanar pieces, one piece drafted (refine merges them,
  which is what New does today).

The fork's checks (`Draft_Modification::Perform`, and the face check in
`BRepOffsetAPI_DraftAngle::Build`) turn each of these into a refusal.

### 2.1 Census of what is still refused

The draft sweep: every planar face of 29 shapes (the fillet sweep's 25 and
#334's four Draft inputs) drafted about each planar face beside it, the
pull direction the neutral plane's normal, at 5, 15 and 60 deg -- 8982
drafts. Classic: 3549 valid, 893 that were invalid are now refused (and
some thousands refused before any of the fork's work, not counted here).
Of the 893, Auto (refine + classic) turns 113 valid (as of 2026-10-05). The
census below is of the remaining 741, each drafted again on the input and
on the refined input with a TKOffset that prints which refusal fired.

| Count | Refusal (classic, on the input) | What it is |
|------:|---------|------------|
| 578 | face check: a self-intersecting wire | a drafted face swept past an edge of a face beside it; that face's outline crosses itself |
| 42 | face check: an inner wire run into the outer one | the same, through a face with a hole |
| 27 | face check: a face turned inside out in the shell | the drafted face turned past its neighbour, nearly all at 60 deg |
| 23 | face check: wires not closed, unorientable faces | |
| 71 | `Draft_VertexRecomputation` (a vertex off an unchanged edge's curve) | a wall split in coplanar pieces; on the refined input 16 are left |

No refusal comes from `FindRotation` or from an edge's or a face's new
geometry: in this sweep every new surface and curve exists. On the refined
input the classes keep their sizes, but for the split walls (71 -> 16), two
edges that turn round (`Draft_EdgeRecomputation`) and 36 drafts that come
out valid -- where OCCT's `UnifySameDomain` merges faces that FreeCAD's
refine guard leaves alone (#334's inputs, whose refine changes the volume).

The first row is the face check: the modification went through, and a
face it rebuilt crosses itself. Every class is a topology change; none is
a numerical miss of the kind the fork's earlier fixes dealt with.

A caution on the inputs: #334's Draft001/002/003 inputs (513 of the 741)
are themselves earlier Draft outputs, and Draft002/003 carry a vertex of
tolerance 27.8 (the stored Draft001 result, `occt-fillet-work` notes). The
measurements in section 5 report them apart.

### 2.2 Not every valid classic result is right

`BRepCheck_Analyzer` checks each face against its own edges and wires, not
one face against another. A drafted face that runs into a different part
of the solid without sharing an edge with it -- #962's ribs, 3.4 apart,
one leaning 4.15 into the next at 15 deg; #334's first input at 60 deg --
leaves two faces crossing, and the classic draft returns that solid as
valid. OCCT's boolean argument check (`BOPAlgo_ArgumentAnalyzer`, Part's
`check(True)`) reports it self-intersecting. The cell draft fuses the
rib into its neighbour instead: a clean solid, 15.76 less volume than
the classic one counts (the overlap, counted twice). Section 5.3 counts
how often this happens among the classic draft's valid results.

Auto takes the classic result as it is, so it hands these on. Catching
them would take a self-intersection check after every classic draft --
of the drafted faces against the rest, not the whole solid -- and then
the cell draft. Its cost is not measured yet (section 9).

## 3. Approaches considered

**(a) Topology repair on the modification (Parasolid's local operation).**
Keep `Draft_Modification`'s geometry and resolve each topological event it
runs into -- an edge shrinking to nothing and turning round, a face
vanishing, a vertex-only contact needing a new edge -- by editing the
topology: merge vertices, delete the edge, insert the edge. This is what
the commercial kernels do. Each event has its own repair and the events
interact (an edge vanishing next to a face vanishing); OCCT has none of
the machinery, and the classic code's own fragility (section 2's list of
refusals took four fixes to make honest) argues against building on it.
Rejected for now.

**(b) The offset engine.** `BRepOffset_MakeOffset` in Complete mode
rebuilds topology by intersecting every face's new surface with its
neighbours' and choosing the valid splits -- exactly a topology-changing
"replace the surfaces" operation, with offset surfaces. A draft would be
the same with zero offset on most faces and a turned surface on the
drafted ones. It is 8000 lines, the other box's active field (thickness),
and zero-offset faces are a case it treats specially. Too much shared risk
for a first version; worth a second look once the cell draft below exists
to compare against.

**(c) A wedge per face, by booleans.** The draft of a planar face adds
the wedge between the old and the new plane on one side of the hinge line
and removes it on the other. Building the wedge as a solid needs its
lateral bounds -- the neighbours' surfaces -- and for a non-convex face
(an L-shaped wall) the wedge is not an intersection of half-spaces. (d)
is (c) without having to build the wedge.

**(d) Cell selection (chosen).** Split space around the solid by the
solid's faces, the drafted face's new surface and its neighbours' surfaces
extended; every resulting cell is entirely in or out of the result; choose
the cells. Everything hard -- the intersections, the new edges, a
neighbour swallowed or extended -- is the general fuse's job, which is
OCCT's most exercised algorithm and keeps a full history of every piece.
What is new is only the choice of cells, which is a flood fill over cell
adjacency with exact rules (section 4.4). Section 5 measures it.

## 4. The cell draft

Terms, for one drafted face `F` of solid `S`:

- `P`: the surface of `F` (a plane, phase 1); `n` its outward normal.
- `P'`: `F`'s new surface -- `P` turned about the hinge line `P x N`
  (`N` the neutral plane) as `Draft_Modification` computes it
  (`FindRotation`; section 4.1).
- `G_1 .. G_k`: the faces sharing an edge with `F` (its neighbours).
- The **swept region** `K`: the space the face passes through on its way
  from `P` to `P'` -- between `P` and `P'`, bounded laterally by the
  neighbours' surfaces.

The result is `(S - K) + (K inside P')`: outside the swept region nothing
changes; inside it, `P'` decides. Where `P'` leans out, the cells of `K`
outside `S` are added (the neighbours grow to meet `P'`); where it leans in,
the cells of `K` inside `S` are taken away (neighbours shrink, and a
neighbour entirely inside `K` is gone). A feature of `S` that lies in `K`
is consumed, as a boolean would consume it.

### 4.1 The new surface

`P'` comes from the same computation as the classic draft:
`Draft_Modification`'s private `NewSurface(S, Oris, Direction, Angle,
NeutralPlane)` and its static `FindRotation`. They move into an internal
helper both use; `Draft_Modification`'s exported members stay, as thin
wrappers (no ABI change). Failures here are the first error class:

- `F` parallel to the neutral plane (no hinge line);
- the angle too steep: `|sin(angle)|` not below the pull direction's
  component across the hinge line (`FindRotation`'s `denom`);
- a surface the draft cannot turn (phase 1: anything but a plane).

### 4.2 The splitting faces

Inside a box `B` around `S`, the general fuse (`BOPAlgo_Builder`, run
non-destructive: a fuse may otherwise raise tolerances on its arguments in
place, and the prototype's sweep did exactly that to its input until each
draft got a copy) takes `B`, `S`, and:

- a face on `P'` covering `B`;
- each neighbour's surface, extended past `B`:

| Surface | Extension |
|---------|-----------|
| plane | a face covering `B` |
| cylinder | a solid cylinder running past `B` at both ends |
| cone | a solid cone, the nappe the face is on, from the apex past `B` |
| sphere, torus | the solid |
| extrusion, revolution, B-spline, offset | `GeomLib::ExtendSurfByLength` by the draft's largest displacement of `F`'s boundary; refused (error) if the extension cannot close the swept region (section 4.4) |

A surface of revolution goes in as a solid, not as its full-turn face.
The prototype first used the face: #523's corner post is a cylinder whose
seam lies in the plane of the box's front wall, another neighbour, and the
fuse lost the cells over the post on one side of it -- a valid solid 0.467
short, exactly the wedge it should have added there (`tan 5 deg` times
`2 r^3 / 3`). As a solid, with the fuse's cells outside `B` dropped, the
draft comes out at the exact volume.

How far to extend: only as far as the swept region can reach, not across
the whole box. The prototype extends every neighbour across `B`, and a
gear-like face of #334 (face 29 of the repaired Draft002 input: a disk
with 94 neighbours, 32 teeth of plane, cylinder, plane) gave a fuse of
some 125 surfaces each cutting all the others; it ran for minutes per
draft and was stopped. The implementation extends each neighbour by the
largest move of `F`'s boundary (section 4.2's margin) beyond its own
face, trimmed to a box around `F`'s swept band, and fuses inside that
box only; the rest of `S` is not split at all. This is part of step 2
of section 8, not a later optimisation.

The cells are the solids of the fuse; each lies entirely in or out of the
result. Which cells are pieces of `S` is the fuse's history, never a
point-in-solid test: on #474's helical part the prototype's point test put
cells worth 2414 "in" a solid of 1887, and the draft came out invalid; by
the history it comes out at the classic draft's volume. (In Python the
history only maps a solid passed as itself, not one inside a compound.)

`B`'s margin around `S` is the larger of `S`'s diagonal and twice the
farthest move of `F`'s boundary (a vertex's distance to the hinge line
times the tangent of the turn). A swept region can be bounded and still
larger than that -- neighbours that flare out, or the far side of a hinge
line crossing the face at 60 deg (#876's frame, a fin 86 long under a
plate 3 thick) -- so a region that reaches `B` is tried again in a box 4
and then 16 times larger before it counts as open (section 4.4).

### 4.3 The drafted face's set

- A neighbour coplanar with `F` (same plane, same orientation -- a face
  split in pieces) cannot bound the swept region: it is drafted with `F`,
  as one face. This is the agreed behaviour of today's New (select one
  piece, the whole merged wall drafts), without refining the rest of the
  body. It differs from the classic draft where that drafts one piece
  alone: `mini_finbase`'s block side and the fin above it are one wall in
  two pieces; the classic draft tilts the lower piece about the hinge
  (+10.94 at 5 deg), the cell draft the whole wall (+10.94 below the
  hinge, -10.63 above it). Auto runs the classic draft first, so only
  `Method = New` sees the difference.
- A neighbour tangent to `F` (a fillet) must follow the draft to stay
  tangent; the classic draft does this by propagating the draft along
  the tangent chain. Phase 1 refuses it with its own error; phase 2 drafts
  the chain (section 4.7).

### 4.4 The swept region

`K` is a set of cells, found by a flood fill:

- **Seeds**: for each piece of `F` (the fuse splits `F` where `P'` and
  the extended neighbours cross it), the cell beside it on the side where
  `P'` lies -- outside `S` where `P'` leans out, inside where it leans in.
  For a plane, the side is the sign of `P'`'s distance at the piece; a
  piece lying in `P'` itself (on the hinge line) seeds nothing.
- **Fill**: from a cell in `K`, cross any face to the cell beyond, except
  a piece of `P'`, of `F` (the old face), or of a neighbour's surface --
  its own face or its extension. Those are `K`'s boundary.
- **Leak**: a cell of `K` that touches `B`'s boundary, in the largest box
  of section 4.2, means the region is not closed: the drafted face does
  not meet some neighbour. That is the second error class -- #474's ramp,
  where the lifted ledge no longer meets the helical ramp beside it. The
  neighbour the leak passed is reported (the last neighbour surface the
  fill ran along).

Two properties make the rules exact rather than heuristic:

- **Add and remove never mix.** The added and removed parts of `K` meet
  only along the hinge line, an edge, never a face; a fill across faces
  stays on one side. So `K`'s cells come labelled: added (seeded outside
  `S`) or removed.
- **A non-convex face needs no special case.** An L-shaped `F`'s
  neighbour surfaces, extended, cut through the column over the other
  arm; that splits the region into more cells, each still seeded from
  the piece of `F` below it.

### 4.5 Choosing the cells

A cell is in the result if it is in `S` and not in `K`, or in `K` on the
added side. The chosen cells must be one piece, joined across faces;
otherwise the draft has cut the solid apart (`SplitsSolid`). A cell of `K`
on the added side that is inside `S` already (a neighbouring feature
reaching into the wedge) stays; one on the removed side that is outside
`S` (a pocket inside the wedge) stays out.

The result's boundary is the faces of the chosen cells that belong to
exactly one chosen cell. Their pieces are put back together by origin,
from the fuse's history: the pieces of one input face become one face
again, and a neighbour's own piece and the pieces of its extension
become one face (the neighbour grown). Pieces are not merged across
origins, and edges of the input are kept: a body with faces split in
coplanar pieces keeps its splits away from the draft. This is
`ShapeUpgrade_UnifySameDomain` limited to pairs of pieces with the same
origin, its history merged into the fuse's (`BRepTools_History::Merge`).

### 4.6 Several faces

PartDesign's Draft drafts several faces with one angle and plane (all the
walls of a boss is the common case). The cell draft drafts them one after
the other, each on the result of the last, the faces found through the
history. A face beside one drafted already meets it with its new
surface, so the corner between two drafted faces is the line of their two
new planes, whichever goes first: at a convex corner the material near the
corner after both is inside both new planes, at a concave corner inside
either, the same as the two half-spaces in either order. A face that an
earlier face's draft swallowed is gone: that is an error (section 4.8),
not a silently skipped face.

### 4.7 Phase 2: tangent chains and turned cylinders

The classic draft drafts a fillet tangent to `F` along with it (the chain
of faces joined by G1 edges), turning a cylinder whose axis is the pull
direction into a cone and rebuilding a fillet between two drafted walls as
an extrusion along their new edge. The cell draft takes the chain as one
sheet: the new faces of the chain, from `Draft_Modification`'s geometry
for the chain alone, joined along their tangent edges, and extended only
across the chain's outer boundary. `P'` in sections 4.2-4.5 becomes that
sheet; the seed side and the "inside `P'`" test become the side of the
sheet's piece a cell touches (its orientation), which needs no plane.
Phase 2 also takes a single drafted cylinder or cone (to a cone).

### 4.8 What the new draft still refuses

Each refusal names the face (and the neighbour, where there is one):

| Error | When |
|-------|------|
| `ParallelToNeutral` | the face is parallel to the neutral plane |
| `AngleTooSteep` | no plane through the hinge line makes the angle with the pull direction |
| `TurnsOver` | the face would turn by 90 deg or more: its new outward normal points against the old one (an undercut face drafted past the pull direction -- #631's walls at 60 deg turn 108 to 113 deg) |
| `UnsupportedSurface` | phase 1: the drafted face is not a plane |
| `TangentNeighbour` | phase 1: a neighbour is tangent to the face |
| `NoClosure` | the swept region leaks to the box: the face does not meet that neighbour (#474's ramp) |
| `FaceVanishes` | the drafted face is not in the result (its neighbours meet across it) |
| `SplitsSolid` | the chosen cells are not one piece across faces: the draft cuts the solid in two (#309's 2 thick plate, its bottom tilted through it) |
| `NotASolid` / `Boolean` | the fuse failed, or the result fails `BRepCheck` or the boolean check (self-intersection) -- every result is checked both ways before it is returned |

`FaceVanishes` is a refusal by default: the user asked to draft a face,
and a result without it is more surprising than an error. It could become
an option.

## 5. Prototype and measurements

`celldraft4.py` (session scratchpad; Python on `Part.Shape.generalFuse`):
phase 1 only -- one planar face, planar or curved (cylinder, cone, sphere,
torus) neighbours, a tangent neighbour refused; the pieces of `S` from the
fuse's map, the other pieces (of `F`, `P'`, the neighbours) recognised
geometrically; neighbours extended across the whole box (not locally,
section 4.2); the box grown on a leak; the result's faces merged by
`removeSplitter` (all of them, not by origin -- good enough to measure
validity and volume). As FreeCAD's New today, it drafts the refined base
when the refine keeps the volume. Each sweep ran one FreeCAD process at a
time, a case that took over 120 s killed and recorded.

### 5.1 The suite's refused cases

| Case | Prototype | Check |
|------|-----------|-------|
| `notch_ledge_a{5,20,44}` | valid, the classic's volume to 1e-12 | |
| `notch_ledge_a45` | valid, 1875 | 1750 + 125 tan 45 |
| `notch_ledge_a60` | valid, 1966.5064 (10 faces) | the ledge rises past the block's top (8.66 over 5); its free sides extend up with it and a fin stands 3.66 over the top: 1750 + 125 tan 60 |
| `notch_bevel_ledge_a{5,20,45}` | valid, the plain notch's volume - 100 (the bevel) | the corner gets its new edge |
| `slot_wall_a5` | valid, the classic's volume exactly | |
| `slot_wall_a15` | valid, 7453.8564 | the wall breaks through at 2/tan 15 = 7.46 above the floor: 7568 - 4 (7.464 + 2 (18 - 7.464)) |
| `slot_wall_a30` | valid, 7437.8564 | likewise at 2/tan 30 |
| `split_floor_corner_a5` | valid, 1151.5110 | the corner slides along the slanted wall to (7.879, -1.0605): 1125 + 5 x 5.3025 |

Every check is closed form and agrees to the printed digits.

### 5.2 The sweep

The 893 drafts that came out invalid before the fork's refusals: the
classic draft makes 39 of them valid (a tolerance fix), Auto (refine +
classic) 127 more, and Auto refuses 727. (Two PartDesign runs disagree on
14 cases, refused in one and valid in the other: section 2.1 counts them
refused, this section valid.) The cell draft on all of them, inputs that
pass OCCT's boolean check apart from those that do not (#334's
Draft002/003, #273's Fillet001, #876's Fillet001 -- see 5.4 for #334's
repaired):

| Result of the cell draft | Clean input, Auto refuses | Clean input, Auto valid |
|--------|------:|------:|
| valid | **275** | 130, Auto's volume to 1e-7 |
| valid, another volume than Auto | | 4 -- Auto's self-intersecting (section 2.2) |
| `TangentNeighbour` (phase 2) | 40 | 16 |
| `TurnsOver` | 28 | |
| neighbour surface not extended (B-spline; the prototype has no `ExtendSurfByLength`) | 4 | |
| `SplitsSolid` | 3 | |
| invalid result, `NoClosure` | 0 | 0 |
| total | 350 | 150 |

On the broken inputs (393 cases) the cell draft mostly refuses
(`SplitsSolid` 304 -- the fuse sees the 27.8-tolerance vertex swallow
whole regions), comes out invalid 30 times and valid 23; 13 died or ran
past 120 s. Repaired (5.4), the same faces draft as on clean inputs.

So on clean inputs the cell draft turns 275 of Auto's 350 refusals --
79% -- into valid solids, and refuses the rest for a reason it can name;
phase 2 (tangent chains) is the next largest class.

### 5.3 The classic draft's valid results

Every sixth valid result of the classic draft in the sweep (the #334
inputs) and all of them on the other shapes -- 1500 drafts, 1222 on clean
inputs. The cell draft on the same faces (on the refined base, as New):

| Result of the cell draft (clean inputs) | Count |
|--------|------:|
| valid, the classic draft's volume to 1e-7 | **1095** |
| valid, another volume, the classic result self-intersecting (section 2.2) | 14 |
| valid, another volume, both clean: the whole merged wall drafted where the classic draft drafts one piece (section 4.3; `mini_finbase`, `mini_finwall`, `mini_slot_*split`) | 12 |
| `TangentNeighbour` (phase 2) | 57 |
| neighbour surface not extended (B-spline) | 21 |
| `TurnsOver`, `SplitsSolid` | 3 |
| invalid, or not boolean-clean: #474 Fillet003, faces beside its helical ramp | 19 |
| total | 1222 |

The last row is the prototype's open defect. Its volumes are within 0.03
of the classic draft's, but the solid is invalid. The ramp's long helical
edges come out with an invalid range after the prototype's global
`removeSplitter` merges them. Without that merge BRepCheck passes, but
the boolean check still reports errors (and the volume is off by 7e-6).
The implementation merges pieces only by origin and keeps the input's
edges (section 4.5), which removes the first cause. The second is still
to be understood, with the fuse's fuzzy value as the first thing to try.
Until then, the result checks of section 4.8 turn such a draft into a
refusal (`NotASolid`) instead of a bad body.

No case was found where the classic result is clean and the cell
draft's differs for any other reason. Where they differ and the classic
result is not clean, the cell draft is the right one.

### 5.4 #334's Draft002 and Draft003 inputs, repaired

Those two inputs fail OCCT's boolean check before any draft: the stored
Draft001 result they are built on has a vertex of tolerance 27.8 at the
drafted ledge's corner, (18, 7.2, 37.017), where two edges of the bevel
beside it still end at the ledge's old corner -- one 26.7 away (z = 63.7),
one 8.25. The bevel's outline is open by 26.7 and the tolerance hides it.
A tolerance repair does not close it: limiting the tolerance leaves the
faces invalid (a self-intersecting wire), and `ShapeFix` puts 26.7 back.

The repair is the draft done right. The cell draft of Draft001 on its own
input (Pad004, clean: face 35 about face 30 at 85.5 deg) is valid and
boolean-clean, with the ledge's corner at (18, 7.2, 37.017) and the
bevel's own corner kept at (18, 7.2, 63.7) -- the new edge the suite's
README says the true result needs -- and no tolerance over 1e-5 (the
input's own). The later features (Pocket005 to Pocket009) cannot be
recomputed (the document stops at Sketch006), so the region of the bad
faces (box (11, -1.6, 15.7) - (19.9, 10.3, 71.2)) is cut out of each input
and the rebuilt Draft001 put in its place. Every valid face of the inputs
inside that box lies on the rebuilt Draft001 to 2e-12, so no later feature
reached into it. Both repaired inputs are valid, boolean-clean, largest
tolerance 1e-5.

The classic draft, same library, same sweep (every planar face against
each planar neighbour, 5/15/60 deg):

| Input | valid | refused | rejected at `Add` | exception |
|-------|------:|--------:|------------------:|----------:|
| Draft002, as stored | 641 | 607 | 48 | 132 |
| Draft002, repaired | 684 | 690 | 42 | 0 |
| Draft003, as stored | 627 | 627 | 48 | 162 |
| Draft003, repaired | 676 | 728 | 42 | 0 |

The cell draft on what the classic draft refuses on the repaired inputs:

| Result | Draft002 | Draft003 |
|--------|---------:|---------:|
| valid | 430 | 465 |
| `TangentNeighbour` (phase 2) | 71 + 42 at `Add` | 71 + 42 at `Add` |
| `SplitsSolid` | 4 | 5 |
| `NoClosure` | 4 | 6 |
| `TurnsOver` | 0 | 1 |
| invalid result | 1 | 0 |
| face 29, the gear disk (section 4.2): the first cases died or ran past 120 s, the rest not run | 180 | 180 |

Leaving the gear face aside, the cell draft makes 430 of 510 and 465 of
548 of the classic draft's refusals valid. On a sample of the classic
draft's valid results (every sixth, 226) it gives the same volume in 215,
refuses 4 (tangent neighbours), and differs in 7 -- and in all 7 the
classic result is self-intersecting and the cell draft's clean.

## 6. API

### 6.1 OCCT (TKOffset)

A new class, so no existing class changes layout:

```
class BRepOffsetAPI_DraftRebuild : public BRepBuilderAPI_MakeShape
{
public:
  BRepOffsetAPI_DraftRebuild(const TopoDS_Shape& S);
  // as BRepOffsetAPI_DraftAngle::Add; false (and Error()) when the face
  // cannot be drafted at all (section 4.1)
  bool Add(const TopoDS_Face& F, const gp_Dir& Direction, double Angle,
           const gp_Pln& NeutralPlane);
  void Build(const Message_ProgressRange& = Message_ProgressRange()) override;
  DraftRebuild_Error Error() const;           // section 4.8
  const TopoDS_Shape& ProblematicShape() const;  // the face
  const TopoDS_Shape& ProblematicNeighbour() const;
  // history: from the fuse, the cell choice and the merges
  const TopTools_ListOfShape& Modified(const TopoDS_Shape&) override;
  const TopTools_ListOfShape& Generated(const TopoDS_Shape&) override;
  bool IsDeleted(const TopoDS_Shape&) override;
};
```

`F`'s new face is `Modified(F)`; a neighbour grown or cut is `Modified`
of the neighbour; a face swallowed is `IsDeleted`; the new edges (the
corner edge of `notch_bevel_ledge`) are `Generated` from the faces they
lie between. FreeCAD's element map needs nothing more (`makEShape` reads
exactly these).

### 6.2 FreeCAD

FreeCAD reaches fork-only OCCT features through `dlsym` (the fillet plate
fallback, `AppPartPy.cpp`), so a FreeCAD package still runs on an OCCT
without them. The same here: TKOffset exports

```
extern "C" BRepBuilderAPI_MakeShape* BRepOffsetAPI_DraftRebuild_New(const TopoDS_Shape*);
extern "C" bool BRepOffsetAPI_DraftRebuild_Add(BRepBuilderAPI_MakeShape*, const TopoDS_Face*,
                                               const gp_Dir*, double, const gp_Pln*);
extern "C" int  BRepOffsetAPI_DraftRebuild_Error(const BRepBuilderAPI_MakeShape*,
                                                 TopoDS_Shape* theFace, TopoDS_Shape* theNeighbour);
```

and FreeCAD drives the object through `BRepBuilderAPI_MakeShape`'s virtual
members (`Build`, `IsDone`, `Shape`, `Modified`, ...) -- which needs no
symbol of the new class -- and deletes it through its virtual destructor.
Without the symbols, New reports that this OCCT has no new draft.

`TopoShape::makEDraft` gains the method; `PartDesign::Draft`:

- **New**: the cell draft. The refine-then-classic of today goes away: the
  cell draft drafts coplanar pieces as one face itself (section 4.3) and
  does not refine the rest of the body.
- **Auto**: Classic; if it fails, the cell draft.
- Errors: the classic draft's refusal is reported with its status and the
  face, edge or vertex it names, as an element name of the base
  (`Draft_EdgeRecomputation` on `Edge12`, ...); the cell draft's with its
  error and face/neighbour names. Auto reports both when both fail.

## 7. Tests

- `tests/fork/draft` (OCCT suite, through PartDesign): the refused cases
  of section 5.1 again with `Method = New`, expecting the closed-form
  volumes there; a #474 ramp case expecting `NoClosure`; a face that
  vanishes, expecting `FaceVanishes`; an L-shaped face; two adjacent walls
  of a boss in both orders (the same solid).
- FreeCAD `TestDraft`: Auto on a slot wall that breaks through (element
  names of the grown and cut faces stable over a recompute).
- The draft sweep with the cell draft, as in 5.2 and 5.3: where the
  classic draft's result is valid and boolean-clean, the cell draft's
  volume is the same (1e-9 relative), but for one piece of a split wall
  (section 4.3); every refused case is either valid and boolean-clean or
  refused with an error from 4.8; the sweep's cases run in the time the
  classic draft takes, within a factor to be set from the measurements.

## 8. Steps

1. Factor `NewSurface` / `FindRotation` (no behaviour change: the draft
   suite and sweep identical).
2. `BRepOffsetAPI_DraftRebuild`, phase 1 (single planar faces, sequential
   faces, all errors of 4.8), the local fuse of 4.2, history, the suite
   cases.
3. FreeCAD: `Method = New` / Auto on it, error reporting for both drafts,
   `TestDraft`.
4. Phase 2 of the algorithm: tangent chains and turned cylinders.
5. Performance beyond the local fuse of section 4.2, if the sweep shows
   the need.

## 9. Open questions

1. `FaceVanishes`: refuse (proposed) or allow?
2. Faces that grow past the body: `notch_ledge_a60`'s fin is the true
   local-operation result (the ledge's free sides extend to meet it). Keep
   that (proposed), or stop a drafted face at the body's other faces?
3. Auto's fallback: the cell draft only (proposed), or keep refine +
   classic before it? The two give the same volume where both work; the
   refine changes faces elsewhere in the body.
4. OCCT class + `dlsym` (proposed), or the algorithm in FreeCAD's Part
   (`TopoShape`), which would need no ABI care but keeps the kernel's
   draft as it is?
5. Auto and the classic draft's self-intersecting "valid" results (section
   2.2): add a self-intersection check of the drafted faces after a
   classic draft, falling back to the cell draft when it fails? It caught
   14 of 1222 valid classic drafts in the sample, and 7 of 226 on #334's
   repaired inputs; its cost is to be measured first.
