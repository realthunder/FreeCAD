# WireJoiner's tight bound search: what is wrong with it

This records a defect in `findTightBound()` that is currently *mitigated* rather
than fixed, so that whoever picks it up starts from the evidence instead of
re-deriving it. Measured 2026-09-16 on `conda-relwithdebinfo-801`.

## The symptom

`Part.joinWires(..., tighten=True)` returns wires that are not tight: some span
several cells, and some are reversed and self-intersecting. Sketches reach this
code through `MakeInternals`, so it affects internal faces, not just the
synthetic case below.

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

`findTightBound()` uses one flag, `iteration2`, for two incompatible purposes.

* A candidate whose mid point tests **outside** the current wire is marked. This
  is monotone-safe: every wire derived later is a sub-region, so outside the
  larger implies outside the smaller. This mark is also what bounds the work.
* Every edge **belonging to** the current wire is marked, so the wire's own
  edges are not treated as splitters. This one is wire-specific, and the wire is
  replaced each time it is split while the mark lives for the whole pass.

So an edge ruled out against an earlier, larger wire stays ruled out against the
smaller wire carved from it. The wire is then declared tight while the edges
that would have split it sit in its adjacency -- live, degree 4, correctly
placed -- merely marked.

The k=31 failure loses exactly one cell, a 1x3 strip at (562.5, 500)-(593.75,
593.75) with a cell size of 31.25. Logging why each candidate was rejected at
the moment a wire is declared done gives, across the three oversized wires:

```
72 MARKED_iteration2    24 self    0 not_inside    0 USABLE_BUT_UNUSED
```

The strip's two interior splitters, at (578.125, 531.25) and (578.125, 562.5),
are both recorded as `MARKED_iteration2 inwire=0`: not part of the wire, not
outside it, simply already marked.

Ruled out first, from a dump of the adjacency list: every edge in and around the
strip has `iteration = 2` (live), and both interior splitters have full degree-4
adjacency at both ends. Across the whole 1984-edge lattice only 4 edges are
swallowed into super edges and 124 are skipped dangling border edges, none near
the strip. The graph the search walks is correct; the search declined edges that
were reachable and valid.

## Why it is not simply fixed

Two fixes were built and measured. Both are wrong, and the reason is the same.

1. **Reset the epoch per wire** (`++iteration2` inside the split loop). k=30 ran
   normally at 17 s, then k=31 burned CPU for over 31 minutes without finishing,
   against 19 s before.
2. **Keep the outside marks, drop the wire-edge marking, and answer membership
   directly with `WireInfo::find()`.** This fixes genuine failures -- k=37 and
   k=39 become correct, at better timings than the mitigation -- but k=31 and
   k=33 do not terminate at all.

The common factor is removing the wire-edge marks, so those marks **are** the
termination mechanism. The search's termination on these inputs depends on the
incorrect early exit: `while (!info.wireInfo->done)` has no independent progress
guarantee, and the stale mark supplies one by suppressing re-examination.
Correctness and termination are in direct tension in the present design.

## What a real fix needs

Separating the two concerns, which is a redesign of the search rather than a
patch:

* Replace the global `iteration2` epoch with a per-wire tag -- give each
  `WireInfo` a serial and skip a candidate only when it was already considered
  against *that* wire.
* Add an explicit progress guarantee: every split must strictly shrink the wire,
  and a wire that has already been derived must never be derived again.

`exhaustTightBound()` marks and tests `iteration2` the same way and needs the
same treatment.

## What is actually in the tree

The adjacency order in `buildAdjacentList()` is now canonical, which stops the
stale marks from colliding: all eleven lattice sizes are correct, `ctest` is
748/748, and element maps over the 37 fixture sketches are unchanged, so no
existing model is renamed. That is a mitigation. The flag defect above is
untouched and still reachable on input that marks edges in an unlucky order.

`addWire()` separately refuses to hand back a wire that self-intersects or whose
face has no area. That guard is sound whatever the cause, and it sits at the
emission point rather than in `initWireInfo()` deliberately: judging every
candidate the search considers measured about a fifth of the tight bound run on
a 40x40 lattice, while judging only the wires actually returned costs nothing
measurable.
