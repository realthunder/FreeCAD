# WireJoiner's tight bound search: what was wrong with it

This records a defect in `findTightBound()`, and the redesign that fixed it, so
that whoever picks this up next starts from the evidence instead of re-deriving
it. Measured 2026-09-16 on `conda-relwithdebinfo-801`.

## The symptom

`Part.joinWires(..., tighten=True)` returned wires that are not tight: some
spanned several cells, and some were reversed and self-intersecting. Sketches
reach this code through `MakeInternals`, so it affected internal faces, not just
the synthetic case below.

A lattice of k horizontal and k vertical lines has `(k-1)^2` cells. Before the
fix, the search returned:

| k | returned | expected |
|---|---|---|
| 31 | 899 | 900 |
| 33 | 1011 | 1024 |
| 37 | 1295 | 1296 |
| 39 | 1443 | 1444 |

k=30, 32, 34, 35, 36, 38 and 40 were correct. The results are deterministic --
three repeat runs of one binary gave the same numbers -- so this was silently
wrong rather than flaky. At k=33 two of the returned wires ran to 62 edges each,
one spanning 58 cells and the other with an area of **-34 cells**; a negative
area means a reversed, self-intersecting wire, which is an invalid face.

## The cause

`findTightBound()` used one flag, `iteration2`, for two incompatible purposes.

* A candidate whose mid point tests **outside** the current wire was marked.
* Every edge **belonging to** the current wire was marked, so the wire's own
  edges were not treated as splitters.

The second is wire-specific, and the wire is replaced each time it is split
while the mark lives for the whole pass. So an edge ruled out against an
earlier, larger wire stayed ruled out against the smaller wire carved from it.
The wire was then declared tight while the edges that would have split it sat in
its adjacency -- live, degree 4, correctly placed -- merely marked.

The k=31 failure loses exactly one cell, a 1x3 strip at (562.5, 500)-(593.75,
593.75). Logging why each candidate was rejected at the moment a wire is
declared done gives, across the three oversized wires:

```
72 MARKED_iteration2    24 self    0 not_inside    0 USABLE_BUT_UNUSED
```

The strip's two interior splitters are both recorded as `MARKED_iteration2
inwire=0`: not part of the wire, not outside it, simply already marked. A dump
of the adjacency list rules out the graph as the culprit -- every edge in and
around the strip is live with full degree-4 adjacency at both ends. The search
declined edges that were reachable and valid.

## Why it could not simply be un-marked

Dropping the wire-edge marking and answering membership directly with
`WireInfo::find()` fixes genuine failures -- k=37 and k=39 become correct -- but
k=31 and k=33 then **do not terminate at all**. Resetting the epoch per wire
does the same. The marks were not an optimisation; they were the termination
mechanism, because `while (!info.wireInfo->done)` has no progress guarantee of
its own and the incorrect early exit supplied one.

Instrumenting that loop to key each pass on the current wire's edge set says
exactly what happens: it is a **2-cycle**. On k=31 the loop alternates between
two wires forever.

```
pass 0 verts=262
pass 1 verts=270      <- a legitimate split RAISED the vertex count
pass 2 verts=268  key=K
pass 3 verts=266
pass 4 verts=268  key=K   <- same edge set as pass 2
```

Note the second line. A split can raise the vertex count, because the splitting
path contributes edges of its own, so wire size is **not** a usable progress
measure. The edge set is.

## The fix

Four parts, and the search needs all four.

1. **Membership is asked, not marked.** The stale "this edge belongs to the
   current wire" marking is gone; the candidate scan asks the wire it is
   actually holding, with `WireInfo::find(next)`.

2. **A split must make progress.** Each run of a split loop remembers the wires
   it has held, keyed on the edge set, and refuses a split that hands back one
   of them; the scan moves to the next candidate instead, and the wire is
   declared tight only when no candidate is left. The set is scoped to the loop:
   different starting edges reach the same wire legitimately.

3. **A refused candidate is unwound completely.** The refusal next to it pops
   exactly one stack frame, which is correct only where it sits, because that
   path is reachable only when `_findClosedWires()` was never called. Once that
   function has *succeeded* it has pushed a frame per edge it walked and
   inserted each into `edgeSet`, and it unwinds those itself only on failure.
   Popping one frame after a success leaves `stack`, `vertexStack` and
   `edgeSet` inconsistent, and the next candidate then assembles a path that
   will not close -- "failed to close some wire" and an `assertCheck(false)`
   abort. `restoreCandidate()` mirrors `_findClosedWires()`'s own unwind.

4. **"Outside" marks belong to a wire, not to a pass.** The remaining global
   marks were justified as monotone-safe: every wire derived later is carved out
   of the current one, so outside-the-larger implies outside-the-smaller. The
   oscillation above disproves the premise -- the loop returns to *larger* wires
   (44 -> 50 -> 44 on k=33, 268 -> 304 on k=31). Once the wire can grow back, a
   mark taken while a smaller wire was current wrongly suppresses a candidate
   that is genuinely inside the larger one, the wire is declared tight with
   splitters still available, and cells are lost. Each `WireInfo` now carries
   the candidates ruled outside *it*, and a wire carved out later starts clean.

Part 4 is not the epoch reset that was tried and rejected. That one bumped the
epoch per pass while *keeping* the stale wire-edge marking and having no
progress guarantee, so it reset both jobs of the flag at once and the search
re-derived wires forever. Here the membership job is gone entirely and the
progress guarantee bounds the passes; only the outside job is being scoped.

## What each part is worth, measured

On the **pristine** source, with the canonical adjacency order deliberately
absent, so that the defect is actually exercised rather than hidden:

| k | baseline | parts 1-3 | parts 1-4 |
|---|---|---|---|
| 31 | 899 wrong | 900 OK | 900 OK |
| 33 | 1011 wrong | 1015 wrong | 1024 OK |
| 37 | 1295 wrong | 1296 OK | 1296 OK |
| 40 | 1521, 42 s | 1521, 44 s | 1521, 45 s |

Parts 1-3 alone still lose 9 cells at k=33: the progress guard converts a
non-terminating oscillation into a *premature* "tight" declaration, which is a
better failure but still a wrong answer. That is what part 4 addresses, and with
it all eleven sizes k=30 to k=40 are correct **without** the canonical order
propping them up. The cost is slight -- k=40 goes 42 s to 45 s against a
baseline that returns the wrong answer -- and the feared blowup did not happen,
because the progress guarantee bounds how many passes a loop can make.

The guard in part 2 fires three times on k=31 and not at all on the other ten
sizes once part 4 is in. With the outside marks correctly scoped the loop mostly
makes progress by itself, so the guard is a backstop rather than the mechanism.

## The canonical adjacency order is kept

`buildAdjacentList()` giving each vertex's adjacency group a canonical order
(`8488e0964b`) was committed earlier as a *mitigation*: it rearranges which
wires form so the stale marks stop colliding, which made all eleven lattice
sizes correct without touching the flag defect at all. It is kept, on its own
merits rather than as a fix -- an order that does not depend on which edge is
asking is worth having for determinism across platforms and OCCT versions, and
it renames nothing in the fixture corpus. The defect it was hiding is now
actually gone, which is what the table above demonstrates.

`addWire()` separately refuses to hand back a wire that self-intersects or whose
face has no area (`95833024c9`). That guard is sound whatever the cause, and it
sits at the emission point rather than in `initWireInfo()` deliberately: judging
every candidate the search considers measured about a fifth of the tight bound
run on a 40x40 lattice, while judging only the wires actually returned costs
nothing measurable.

## What is in the tree

The fix is two commits on `SketcherPort`: `38d362dd43` (parts 1 to 3) and
`09c95bd334` (part 4). Both are individually buildable and neither hangs, so
the history does not pass through a broken state -- parts 1 to 3 alone are
already correct on all eleven lattice sizes with the canonical order present.

Verified on the second of them:

* lattice k=30 to k=40, all eleven correct;
* `ctest` 748 of 748, 0 failed;
* `TestSketcherApp` 97 OK, with all 46 `TestSketchInternalFaces` cases present;
* element maps over the 37 fixture sketches, 38 of 38 sections identical to the
  pre-change dump after masking hasher ids -- **zero rename**, which is what
  makes this safe to land in a fork that treats element names as an API;
* the Python suite, 2854 OK, 50 skipped, 6 expected failures.

There is no automated regression test for this defect. The smallest reliable
reproducer is a k=31 lattice at roughly 20 seconds, which is too slow for the
unit suites, and `tests/src/Mod/Part/App` has no WireJoiner suite to host one.
It is guarded by the sweep below, run by hand.

## Reproducing

The scripts are in the durable scratch directory
`~/.claude/projects/-home-thunder-works-sw-fcad/scratch/wirejoiner` (README
there): `onek.py <k>` for one lattice's wire count, `areas.py <k>` for the
area/cell histogram that shows *which* cells were lost.

Two things to know before running a sweep. A regression here **hangs** rather
than fails, so every run needs its own timeout. And `FreeCADCmd` exits 0 even
when the script it runs raises, so success has to be judged on the count line
appearing, never on the exit code.
