# OCCT 7.7.2 backports: policy and ledger

Ruling (user, 2026-08-13): **development targets OCCT 8.0.1 only**
(occt branch `LinkVibe-801`). Nothing is ported to the 7.7.2 branch
(`LinkVibe`) by default; a backport happens only with a compelling
reason -- a shipped-package crash, data corruption, a security issue,
or a correctness bug a released user actually hits. "The same bug
exists there" is not by itself compelling: the released packages have
carried these codepaths for years, and every backport costs a branch
switch, a second build stack, and a divergent verification story.

This file is the single place 7.7.2 porting work is tracked. If a
change on `LinkVibe-801` looks like it deserves a backport, add it to
the candidate list here (with the reason) instead of porting it.

## What the 7.7.2 branch still is

- occt branch `LinkVibe` (7.7.2), sibling repo `~/works/sw/occt`.
  The released realthunder packages link it; the fcad build that
  compile-checks the version-guarded fallback paths is the
  `conda-debug` tree against `occt/install/conda-debug`.
- The fcad sources keep `OCC_VERSION_HEX` guards for 7.7.2. Those
  guards stay (cheap, and they document which OCCT APIs moved), but
  they are no longer routinely compile-checked; expect bit-rot and
  budget for it if a 7.7.2 build is ever revived.

## Ledger

### Ported (before this policy)

- **B-spline Resolution lazy-cache race** -- occt `LinkVibe` commit
  `d7a87a6e72`, the port of `LinkVibe-801` commit `00410c1536`
  (atomics on maxderivinv/maxderivinvok in all six
  Geom/Geom2d B-spline/Bezier classes; found by TSan, eleven
  reports, one wrong-value-visible variant). Verification state:
  the occt 7.7.2 tree itself rebuilt and installed clean
  (`build_conda_debug` -> `install/conda-debug`); the fcad-side
  compile check of the fallback tree was INTERRUPTED by this policy
  and never finished. The commit stays -- it is self-contained
  inside occt -- but nothing has proven the fcad 7.7.2 stack against
  it end to end.

### Candidates (none accepted)

- (empty)

## State of the 7.7.2 build stacks, frozen by this policy

- fcad `build/conda-debug`: cache flipped to `BUILD_ENABLE_CXX_STD=
  C++20` mid-rebuild (the tree predates the C++20 migration and no
  longer compiled at C++17 -- the known cache trap) and the rebuild
  was stopped partway. The tree is INCONSISTENT: do not trust
  binaries out of it without a full rebuild.
- occt `build_conda_debug` / `install/conda-debug`: consistent, at
  `d7a87a6e72`.
- The occt working tree is back on `LinkVibe-801`; switching to
  `LinkVibe` for any future backport must switch back afterwards --
  the 801 build dirs build from the working tree.
