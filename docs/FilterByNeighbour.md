# Filter by neighbour: breaking the tie in `getRelatedElements`

Status: **plan only**, written 2026-08-26. No code has been written. Requested
by the user as "give me a plan on adding the filter by neighbor in
getrelatedelements".

Companion reading: `docs/UpstreamNameMap.md` (the naming translation table),
and the ruling recorded alongside it that a missing element map is tolerated
by design and naming diagnostics are developer-only.

## 1. What we already have, and what we do not

The premise this plan corrects: **we are not missing a fallback.** The fork
already resolves a lost element reference, and it does so by ancestry.

    ComplexGeoData::getElementHistory       src/App/ElementMap.cpp:1725
        Peels one MappedName backward: find the tag postfix, de-hash, strip one
        encoding layer, repeat while the tag holds. Yields the original name
        and the intermediate history.

    getElementSource                        src/Mod/Part/App/PartFeature.cpp:433
        Iterates that across objects by tag, fetching each owner's shape as it
        goes. Yields the full ancestry chain as
        `std::vector<std::pair<long tag, MappedName>>`, deepest last.

    Feature::getRelatedElements              src/Mod/Part/App/PartFeature.cpp:701
        Given a name that no longer resolves, walks every live sub-shape of the
        same type, builds each one's ancestry chain, and scores it by how deep
        the chain matches the lost name's chain from the tail. The best score
        lands in `retMap`, and the function returns `retMap.begin()->second`.

That is a real and working history-based fallback. **The gap is a tie.**

`retMap.begin()->second` is a `QVector<Data::MappedElement>`. When several live
elements share an equally deep ancestry chain -- the ordinary case where one
face was split into three by a later feature, so all three descend from the
same source face -- every one of them comes back, and nothing chooses between
them.

Nothing downstream chooses either. Every consumer takes the first:

| Consumer | Line | What it does with the tie |
|---|---|---|
| `TaskSketchBasedParameters.cpp` | 453 | `related.front()`, then `FC_WARN("guess element reference ...")` |
| `TaskSketchBasedParameters.cpp` | 891 | `related.front()`, and `break`s out of the loop |
| `TaskTransformedParameters.cpp` | 370 | first entry |
| `ReferenceSelection.cpp` | 349 | iterates, first match wins |
| `Utils.cpp` | 1729 | iterates, first match wins |
| `DlgFilletEdges.cpp` | 656 | iterates, first match wins |
| `ViewProviderExt.cpp` | 3119, 3795 | iterates |
| `Attacher.cpp` | 940 | first entry |
| `SketchObject.cpp` | 9398 | first entry |

`front()` of a tie is the sub-shape that OCCT happened to enumerate first. It
is not a guess informed by anything. That is what this plan replaces.

## 2. What OCCT's FILTERBYNEIGHBOURS actually does

Reference implementation:
`~/works/sw/occt/src/ApplicationFramework/TKCAF/TNaming/TNaming_Name.cxx`,
`FilterByNeighbourgs` at line 1241 on branch `LinkVibe-801`. Read in full
2026-08-26; the algorithm is small.

    Args.First()          the collection of candidates to filter
    Args rest             one TNaming_NamedShape per recorded neighbour
    ShapeType             the type being named

    TC = VERTEX if naming an EDGE, else EDGE       (the boundary type)

    for each candidate S in CurrentShape(Args.First()):
        Boundaries = { every TC sub-shape of S }   (S itself if S is a vertex)
        Keep = true
        for each neighbour argument NSVois:
            SVois = CurrentShape(NSVois)           (resolve it forward to today)
            Connected = any TC sub-shape of any shape in SVois is in Boundaries
            if not Connected: Keep = false; break
        if Keep: select S

So: **a candidate survives if it shares a boundary sub-shape with every one of
the recorded neighbours.** Boundary of a face is its edges; boundary of an edge
is its vertices; a vertex is its own boundary.

Two properties matter for us.

1. It is a **filter over a candidate set**, not a search. It presumes something
   else already produced the candidates. That is exactly the shape of our
   problem: we have a tied `QVector` and need to cut it down.
2. It is **not history-free**. `Args` rest are `TNaming_NamedShape` handles --
   references recorded at selection time and re-solved forward through the
   modelling history by `CurrentShape`. OCCT knows its neighbours because it
   wrote them down when the selection was made.

Point 2 is the one design question this plan has to answer: **where do our
neighbours come from?** We never wrote any down.

## 3. Where our neighbour set comes from

Two possible sources. The recommendation is A.

### Option A (recommended): derive the neighbours from the source shape

At the moment of the tie we already hold everything needed:

- `source` -- the lost name's ancestry chain, from `getElementSource`.
- `idx` -- the depth at which the tied candidates matched, i.e. the deepest
  common ancestor `source[idx]`, a `(tag, MappedName)` pair.
- The tag resolves to a document object, whose shape we can fetch with the
  same `Feature::getTopoShape` call `getElementSource` already makes on its
  way down.

In that source shape, `source[idx].second` **still resolves to a live indexed
sub-shape** -- that is what made it the matched ancestor. So we can read its
boundary directly:

    sourceShape = getTopoShape(objectForTag(source[idx].first))
    origin      = sourceShape.getIndexedName(source[idx].second)
    originBoundary = { mapped names of the TC sub-shapes of origin }

and for each tied candidate, map its own boundary back to the same object:

    candidateBoundary = { deepest ancestor in objectForTag(source[idx].first)
                          of each TC sub-shape of the candidate }

Then score each candidate by `the number of candidate boundary names found in originBoundary` and keep
only the maximum. This is OCCT's rule with the direction of travel reversed:
OCCT resolves recorded neighbours forward to today, we resolve today's
neighbours backward to the recorded element. The comparison is the same
comparison.

Why this is the right one for this fork:

- **No format change.** Nothing new is persisted, so it fixes references that
  already exist in saved documents. Option B only ever helps references created
  after it ships.
- **It reuses the machinery that is already correct.** `getElementSource` is
  the only thing that has to be called again, on the boundary elements, and it
  is already the function the tie came out of.
- **It degrades to today's behaviour.** If the source object is gone, or the
  source element no longer resolves, or every candidate scores the same, the
  filter returns the tie untouched.

The honest cost: the boundary of a face in the current shape may itself be a
tie (an edge split alongside the face it bounds). That is fine -- we are
scoring by intersection size, not requiring a unique match, and a partial
boundary match still discriminates a split face from its siblings, because a
split gives each fragment a *different subset* of the original's edges. The
degenerate case where two fragments inherit identical edge subsets is a genuine
ambiguity that no neighbour rule can resolve.

### Option B (not recommended now): record the neighbours at selection time

Faithful to OCCT: when a reference is created, store the mapped names of the
selected element's boundary alongside the reference, and at resolution time
resolve each recorded name forward and apply OCCT's rule verbatim.

Against it: it needs a persisted-format change on every property that holds an
element reference (`PropertyLinkSub`'s shadow subs and everything built on
them), it does nothing for existing documents, and it duplicates information
already recoverable from the ancestry chain. Worth revisiting only if Option A
measures as too slow or too weak in practice.

## 4. Where the code goes

One function, `Feature::getRelatedElements`, `src/Mod/Part/App/PartFeature.cpp`.

Today's tail:

```cpp
    if(retMap.size())
        ret = retMap.begin()->second;
    shape.cacheRelatedElements(mapped.name,sameType,ret);
    return ret;
```

Proposed:

```cpp
    if(retMap.size()) {
        ret = retMap.begin()->second;
        if (ret.size() > 1)
            filterByNeighbour(owner, shape, source, retMap.begin()->first, ret);
    }
    shape.cacheRelatedElements(mapped.name,sameType,ret);
    return ret;
```

Three properties this placement buys:

1. **It runs only on a tie.** `ret.size() > 1` is the guard. A unique match --
   the common case, and the whole fast path above it, including the early
   `getIndexedName` return at line 726 -- never reaches the filter, so nothing
   that works today gets slower.
2. **It is a filter, never a search.** It may only shrink `ret`; it may never
   add a candidate, and it must return the input unchanged when it cannot
   decide. That keeps it strictly an improvement on `front()`.
3. **It lands before the cache.** `cacheRelatedElements` stores the answer for
   subsequent lookups, so the filtered result is what gets cached and the cost
   is paid once per shape.

New static function in the same file, next to `getElementSource`:

```cpp
// Break a tie among elements with equally good ancestry by comparing their
// boundaries against the boundary of the ancestor they share -- the rule
// OCCT calls FILTERBYNEIGHBOURS (TNaming_Name.cxx, FilterByNeighbourgs).
// Shrinks `candidates` in place, and leaves it alone if it cannot decide.
static void filterByNeighbour(App::DocumentObject *owner,
                              const TopoShape &shape,
                              const std::vector<std::pair<long,Data::MappedName>> &source,
                              int depth,
                              QVector<Data::MappedElement> &candidates);
```

## 5. Steps

1. **A failing test first.** Build the tie in a document: a box, a feature that
   splits one face into several, and a reference to the original face taken
   before the split. Assert that `getRelatedElements` returns more than one
   candidate today, and name which one is correct. `TopoShapeEx_tests_run` is
   the wrong suite -- this needs live `DocumentObject`s and tags, so it belongs
   with the Part feature tests (`tests/src/Mod/Part/App/FeaturePart*.cpp`) or a
   Python test in `src/Mod/Part/App/`. Without this test the rest is unfalsifiable.

2. **`boundaryType(TopAbs_ShapeEnum)`** -- FACE -> EDGE, EDGE -> VERTEX,
   VERTEX -> VERTEX. Three lines, mirroring OCCT's `TC`. Anything else returns
   `TopAbs_SHAPE`, and the filter declines.

3. **Resolve the shared ancestor.** From `source[depth]`, get the object by tag
   (the same `doc->getObjectByID(tag < 0 ? -tag : tag)` dance `getElementSource`
   does at line 470) and its shape. Bail to the unfiltered tie if either is
   gone -- an object may legitimately have been deleted.

4. **Collect `originBoundary`.** Resolve `source[depth].second` to an indexed
   name in that shape, take its boundary sub-shapes, and record their **mapped**
   names. Mapped, not indexed: indices in the source shape mean nothing in the
   current one.

5. **Score each candidate.** For each tied candidate, walk its boundary
   sub-shapes; for each, call `getElementSource` and take the entry whose tag
   matches `source[depth].first`; count how many of those land in
   `originBoundary`.

6. **Keep the maximum, only if it is strict.** If the top score is shared by
   every candidate, or is zero, leave `candidates` alone -- reporting a tie is
   honest, and silently narrowing to an arbitrary member is the bug we are
   fixing. Otherwise keep only the top scorers.

7. **Diagnostics, developer-only.** One `FC_LOG` line naming the tie size, the
   scores, and the survivor. Per the naming-diagnostics ruling this must not be
   a user-visible message: a guessed reference is already marked in band by
   `GeoFeature::getElementName` mangling it to `?Face7`.

8. **Measure the fallback path.** The filter calls `getElementSource` once per
   boundary sub-shape per candidate. For a tie of 3 faces of 4 edges each that
   is 12 ancestry walks -- fine. For a tie of 200 faces it is not. Cap the
   candidate count (and `log()` when the cap bites, never truncate silently),
   or memoise `getElementSource` per `(tag, name)` across the call. The
   memo is the better fix and is trivially correct: the chain for a given name
   in a given shape does not change during one resolution.

## 6. What this does not fix

- **A genuine ambiguity.** Two fragments that inherit the same boundary subset
  from the same ancestor are indistinguishable by neighbour, and by anything
  else short of geometry. The filter must return both, not pick one.
- **A deleted source object.** The chain still walks (the hasher can produce
  the original name), but there is no shape to read a boundary from, so the
  filter declines.
- **Reordering in PartDesign::Body.** The existing code deliberately compares
  ancestry by name and ignores the tag (`rit->second != source[idx].second`,
  with the tag-sensitive comparison sitting `#if 0`'d beside it) so that
  reordering still matches. The neighbour filter must match that choice --
  compare boundary ancestry by name too -- or it will reject the very
  candidates the loose comparison was written to admit.

## 7. Estimated size

| Piece | Lines |
|---|---|
| `boundaryType` helper | 10 |
| `filterByNeighbour` | 80-110 |
| Call-site guard in `getRelatedElements` | 4 |
| The failing test, then passing | 60-90 |

One commit, `Part:` prefixed, one problem. No header change: `filterByNeighbour`
is static to `PartFeature.cpp` and `getRelatedElements` keeps its signature, so
no ABI question arises.
