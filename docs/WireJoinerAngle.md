# WireJoiner finds the minimal wires by angle, not by search

`WireJoiner` used to find its tight bound wires by searching: walk the adjacency
list, take the first candidate that passes a point-in-face test, backtrack when
the path does not close, then split the wires that turned out not to be minimal.
That search is what [WireJoinerTightBound.md](WireJoinerTightBound.md) is about
-- what was wrong with it, and the four-part redesign that made it correct.

On planar input it is no longer used. The minimal wires are now read off
directly from the angular order of the edges at each vertex. Written and
measured 2026-09-17 on `conda-relwithdebinfo-801`.

## The rule

Take a **dart** to be one end of an edge, meaning that edge travelled away from
that end. `WireJoiner::VertexInfo` already is one -- an edge iterator plus a
flag saying which end -- and `adjacentList` already holds, for each vertex, the
darts leaving it. Then

    rev(d)   the same edge travelled the other way
    next(d)  the first dart clockwise from rev(d) in the angular order of the
             darts leaving the vertex d arrives at

`next()` is a permutation of the darts. Walking it from any dart returns to that
dart, every dart lies in exactly one orbit, and each orbit is a face boundary of
the network. Every edge has exactly two darts, so each side of each edge is
walked exactly once, and the whole enumeration is one pass over the darts plus
one angular sort per vertex.

Nothing is searched and nothing is undone. `rev(d)` is always among the
candidates, so `next()` is a total function and there is no dead end to back out
of. Dangling tails and spirals need no special case either: at a degree-1 tip
the only dart leaving is `rev(d)`, so the walk turns around, and a bridge edge
simply has both of its darts in the same orbit.

This is the rule OCCT walks a branching vertex with --
`BOPAlgo_WireSplitter::ClockWiseAngle`, in the face's parametric space --
including its two details: going straight back is promoted to a full turn so it
is only ever a last resort, and a vertex with one way out takes it without
consulting angles.

### Where OCCT's "each edge twice" fits

OCCT reaches the two darts differently. Its edge set holds shapes, not ends, and
each shape contributes an entry at each of its two vertices, tagged in or out by
orientation -- so one way out per copy. An edge that has to be walked on both
sides is therefore put in the set **twice**, once forward and once reversed
(`BOPAlgo_BuilderFace` fills it that way, and `SplitBlock`'s `IsInside` flag is
how it tells those apart), while an edge that only ever bounds the face from the
inside goes in once.

Here every edge is in both directions always, because there is no face to take a
side from. That is the same enumeration, plus one consequence: the walk also
produces the **unbounded** face of each connected component, which OCCT's
asymmetry suppresses for it. Dropping those is the signed area filter below.

## The two filters

An orbit is a closed walk, not yet a wire. Two things are removed before one is
emitted.

**Out and back excursions.** A bridge edge has both darts in the same orbit and,
once whatever lies beyond it has itself been removed, they sit next to each
other. The pair bounds nothing, so it is dropped; an orbit that collapses to
nothing this way was a tree. Those edges come back as open wires, which is where
the search left them too. (Most tails never get this far: `buildAdjacentList()`
already drops edges connected at one end only.)

**The unbounded face.** Under the turn rule above, every bounded face comes out
traversed counter-clockwise and the unbounded face of each component clockwise,
so the sign of the loop's area decides. The area is a shoelace sum in the common
plane over points sampled along the loop, and *how* they are sampled is the one
place this went wrong in development:

* a loop of two arcs has only two vertices, and its chord polygon is empty, so
  each edge contributes its mid point as well;
* a **merged** edge stands for a whole chain of edges, and its own two ends say
  nothing about where that chain went. Two overlapping rectangles reduce to four
  chains between two vertices -- every face boundary is two darts long -- and
  sampling only dart ends puts every area at zero. `dartSamples()` walks the
  chain and samples every edge in it.

That second one is in `regression_tests.py` as
`test_joinWires_overlapping_rectangles`, because the symptom is quiet: the
traversal is right, all four faces are found, and the filter then throws three
of them away.

## Ties

Two edges leaving a vertex in the same direction -- a line meeting a circle
tangentially, say -- have the same angle, the successor is ambiguous, and two
faces would silently merge. Candidates within `AngleTie` (1e-8 rad) of the best
turn are therefore re-measured with the chord to a point a tenth of the way
along each edge instead of the tangent at the vertex, which is what OCCT's
`RefineAngles`/`RefineAngle2D` do for the same reason.

## The gate

The rule needs a consistent cyclic order of the darts at each vertex, which
needs one 2D parameter space to measure the turns in. **A common plane is the
requirement in practice**, and `findCommonPlane()` asks `TopoShape::findPlane`
over the input once, in `build()`.

Planar is sufficient but not the true requirement -- OCCT's version runs on any
face's parametric space, so a cylinder or a NURBS patch would do -- but it is
what can be found cheaply here, and it is what every caller in the tree has:
Sketcher internals, `Part::Face`, `SubShapeBinder`, `FaceMakerBullseye`. Only
`Part.joinWires` can be handed anything. Without a common plane the search runs
exactly as before; it is still the only thing that serves a 3D edge network
spread over several surfaces.

`WireJoiner::setAngleTraversal(false)`, or `Part.joinWires(..., angle=False)`,
forces the search with nothing else changed. The default is on. It is there to
A/B the two on any input, which is how everything below was checked, and as a
way out if a real model turns up that the rule handles worse.

The gate only applies to the tight bound and outline paths. Plain
`findClosedWires()` -- `tighten=False, outline=False` -- has a different
contract (as many edges as possible in closed wires, minimal or not) and is
untouched.

## Measured

Lattices of k horizontal and k vertical lines, `(k-1)^2` cells, one process per
measurement, `Part.joinWires(split=True, merge=True, tighten=True)`:

| k | search | angle | speedup |
|---|---|---|---|
| 20 | 4.21 s | 0.085 s | 50x |
| 25 | 9.24 s | 0.146 s | 63x |
| 30 | 16.85 s | 0.215 s | 78x |
| 35 | 25.73 s | 0.317 s | 81x |
| 40 | 41.52 s | 0.437 s | 95x |

Both columns are correct: every size from k=20 to k=40 returns exactly `(k-1)^2`
wires whose areas sum to the region, including k=31 and k=33, the sizes the
search got wrong before its redesign. The angle column grows about linearly in
the edge count while the search column does not, so the ratio keeps opening up.

The point-in-face test is what left: `isInside()` ran a
`BRepClass_FaceClassifier` inside the search loop, and in this formulation
containment is not needed at all -- the angular order decides the successor, and
the only geometry left is one tangent per dart and one shoelace per loop.

## Verified

Beyond the lattices, the two modes were compared directly on arrangements the
lattice does not reach -- arcs and a chord, an annulus cut by radial lines, two
and three overlapping rectangles, a tangency, disjoint and nested components, a
bridge between two loops, a dangling tail, a six-way star, and a non-planar
network for the gate -- with and without `outline`. All identical.

Then the standing battery: `ctest`, `TestSketcherApp` including the 46
`TestSketchInternalFaces` cases, the element maps of the 37 fixture sketches
against their stored baseline (this fork treats element names as an API, so a
rename is a regression), and the full Python suite.

## Independent of the adjacency order

The search took whatever candidate the adjacency list offered first, which is
why a canonical order per vertex (8488e0964b) was once needed for the same
input to give the same wires on every platform, and why it was reverted when
it turned out to cost 2.3x. The angle rule never consults that order: the
successor is the tightest turn, decided by geometry alone, and the orbits are
walked from the edges in input order. So the result should not depend on how
the adjacency groups happen to come out of the R-tree, and it does not.
Measured 2026-09-17 with each vertex's adjacency group reversed, sorted by
angle, and sorted by a lexicographic key with no geometric meaning:

* the ten shapes above and lattices k=20, 25, 31, 33, 40, with and without
  `outline`: 30 of 30 results identical, including the order the wires are
  emitted in;
* the element maps of the 37 fixture sketches: 38 of 38 sections identical.

The non-planar control case, which the gate hands to the search, changes its
emission order under the reorderings -- as the search always did -- and it
exposed a crash in the search that the built order had kept out of reach
(c049fd09c2: a wire that does not make a face was tightened anyway).

## Where the time goes now

`perf` on the k=40 lattice, angle path, 2026-09-17, as a share of `build()`,
before the wires were assembled directly (below):

| stage | share | what it is |
|---|---|---|
| `addWire()` | 45% | `ShapeFix_Wire` + `BRepBuilderAPI_MakeFace` + `BRepGProp` + self-intersection check per emitted wire |
| `splitEdges()` | 30% | R-tree nearest queries in `add()` |
| `buildAdjacentList()` | 22% | R-tree nearest queries |
| `findAngleWires()` | below 0.3% | the traversal |

On 200 nested circles (every bounding box contains the smaller ones, no
intersection at all) 83% was `checkIntersection()`: the per-pair face
construction. That is gone since e2a5ccef1f, below.

## The wires are assembled from the darts

`addWire()` used to hand every wire to `ShapeFix_Wire`, make a face of it,
and check the face for self-intersection and area. The traversal has
already settled all of that: an orbit's darts are connected and in order,
`add()` made every coincident vertex the same `TopoDS_Vertex` when the edge
came in, the edges were split at every crossing before the walk so the
boundary cannot cross itself, and the signed area that admitted the orbit
is the area the face check would have measured. `makeDartWire()` now builds
the wire with `BRep_Builder` straight from the darts, and an orbit wire
skips the checks. The search path keeps them; its wires are candidates,
not face boundaries.

The one thing the fixer had been doing was orienting. A dart is defined by
the parametric ends of its edge, but the edge as stored may be `REVERSED`
(`connectEdge()` rebuilds edges through `BRepLib_MakeWire`, which flips
them to connect), and a merged chain is stored the way round it was walked
when it was made. Adding the edges as stored produced 82 negative cells in
the k=20 lattice and turned three overlapping rectangles into areas of -20
and -6 that still summed to 217. The orientation now comes from the dart:
`FORWARD` when it leaves the parametric start, and a chain is reversed when
the dart leaves its tail. Pinned by
`test_joinWires_orients_reversed_input_edges` and by the exact per-region
areas in `test_joinWires_overlapping_rectangles`.

k=40 goes from 0.38 s to 0.20 s, 200 nested circles to 0.008 s, and the
element maps over the 38 fixture sections are identical, indices included.
What was left was almost entirely the R-tree nearest queries: `add()` inside
`splitEdges()` about 55% of `build()`, `buildAdjacentList()` about 41%.

## The vertex lookup is a grid hash

Every question asked of the vertex index is "which edge ends lie within
the tolerance of this point": `add()` asks it to find the vertex a new
edge should share and to reject duplicates, `buildAdjacentList()` to
gather each vertex's adjacency group. The R-tree answered it with an
incremental nearest-neighbour walk (`bgi::nearest(pt, INT_MAX)`, stopped
by the caller once the distance exceeded the tolerance), and that walk was
nine tenths of `build()` once nothing else was left.

`VertexIndex` hashes each point quantised on a grid of twice the tolerance.
The ball of radius tol around any point lies inside the 2x2x2 block of
cells around it, so a query is at most eight probes of an
`unordered_multimap`, filtered by distance. The entries come back sorted
by distance and then by insertion order, so the adjacency order does not
depend on how the table is laid out -- the R-tree's tie order was an
artefact of its splits. The cell size follows `myTol` at `clear()`, and a
query with a wider tolerance than the grid was built for rehashes first.

k=40 goes from 0.20 s to 0.022 s; 17x since the morning, 1900x since the
search. `build()` is now a twentieth of the process: the rest is startup.
Inside it `splitEdges()` is three quarters, most of that `add()`, and most
of *that* the `BRepLib_MakeWire` that `connectEdge()` goes through to
give a fragment its shared vertices. The lookup itself is under a tenth.

## The search assembles its wires the same way

The plain `findClosedWires()` -- `tighten=False`, the `Part::Face` and
`SubShapeBinder` default -- and the search fallback both close a loop as a
list of darts: the one the search started from and the one chosen at each
level of its stack. They are connected in that order and share their
vertices, so `makeDartWire()` assembles them too. `ShapeFix_Wire` had been
reordering an ordered sequence at a cost quadratic in its length, and the
plain search makes its wires as long as it can: on the k=40 lattice
`ShapeAnalysis_WireOrder` alone was 45% of the 6.4 s this path took. It
now takes 0.48 s, and what is left is the search's own bookkeeping.

What changes on this path is how a wire is rotated and which way round it
runs. The fixer took the first edge in whatever orientation `connectEdge()`
had stored it and chained the rest to fit, so a loop whose first edge was
stored `REVERSED` came out travelled backwards and re-chained from wherever
`ShapeAnalysis_WireOrder` chose to begin. A wire now starts at the dart the
search started from and runs the way it was walked. Over the 37 fixture
sketches and the synthetic cases, plain path, with and without merging (98
runs): the set of edges in every wire is identical and so is every element
name once its hasher ids are expanded, and 18 runs differ in rotation or
direction only. Two things to know when reading such a comparison: the ids
inside a name (`#e`, `#11`) are handed out in the order names are first
built, so a change in edge index order changes them without changing what
they stand for, and the number after the hasher id is the length of the
postfix text, which follows the digit count of those ids. `plainab.py`
dumps names with the ids expanded; `plaincmp.py` compares membership and
names and reports rotation separately.

## The 2D intersector, and what names follow

`splitEdges()` used to build a wire, a face and a `ShapeAnalysis_Wire` for
every candidate pair. It now finds one plane for the whole input, builds each
edge's 2D curve once, and intersects the pairs with `Geom2dInt_GInter`
directly (e2a5ccef1f, the former `WireJoiner2d` branch). 800 nested circles
go from 51 s to 0.09 s; OCCT's `generalFuse` needs 3.1 s on the same input.
Pairs with no common plane keep the `BRepExtrema` fallback.

The branch had sat unlanded because two fixture sketches renamed a face
under it, and the cause turned out to be worth writing down. The 2D
intersector puts a crossing at x = -8.9e-16 where the old path hit 0
exactly. Topology is the same. But two things downstream were reading the
bits:

* **The order the wires came out in** followed the walk, i.e. which fragment
  was created first and which end of a merged chain became its
  representative. The Sketcher names its faces from the wires in order
  (`FaceMaker::postBuild` takes the first edge names no earlier face used),
  so the face names followed the order. `build()` now sorts the finished
  wires by the set of input edges they are made of (8f8e803e05); each
  `EdgeInfo` carries those indices through splits and merges.
* **Which edge a fragment is named after.** A fragment is `Modified` from the
  edge that cut it at its start, and when two edges cut at the same point --
  a vertical crossing a line exactly where a collinear edge starts
  overlapping it -- `pushIntersection()` kept whichever pair the R-tree
  offered first. It now keeps the better one by rule: a crossing beats an
  overlap end, then the smaller source set (c211be8bad).

The intersector also reports what the old path did not: two edges that share
a stretch come back as a *segment*, and both of its ends are splits. The old
`ShapeAnalysis_Wire` call took one point per segment by a rule of its own and
so left one collinear overlap uncut. The one fixture that changes is
`test_28534_truncated_pocket` Sketch019, a rectangle whose right side is three
collinear pieces with a 0.01 overlap: 6 edges instead of 5, the same 150 unit
face, and no edge lying on another. Pinned by
`test_joinWires_splits_a_collinear_overlap`. Every other fixture sketch, 37
of them, has an element map identical to the 3D path's, compared unmasked.

Names that followed the bits were a hazard under any OCCT upgrade or
platform change, not just under this rewrite. Both rules are in the 3D path
too.

The plain `findClosedWires()` -- `tighten=False`, the `Part::Face` and
`SubShapeBinder` default -- takes 6.4 s on the same k=40 lattice against the
angle path's 0.45 s, 45% of it in `ShapeAnalysis_WireOrder::Perform` inside
`makeCleanWire()`, because its wires are as long as possible and the reorder
is quadratic in a wire's edge count.

## Reproducing

Scripts are in the durable scratch directory
`~/.claude/projects/-home-thunder-works-sw-fcad/scratch/wirejoiner`. `onek.py
<k>` counts one lattice's wires; passing `angle=False` to `Part.joinWires` is
the control for any input.

Two things to know. A regression in the search **hangs** rather than fails, so
every run needs its own timeout. And `FreeCADCmd` exits 0 even when the script
it runs raises, so success has to be judged on the result line appearing, never
on the exit code.
