# Render publish glossary

The vocabulary of the publish path -- how a changed shape becomes draws
in a frame, on the desktop renderer and on the streamed tier. Written
because the terms are load-bearing and easy to conflate: "0 adopted"
was read for two sessions as a statement about ADOPTION when it was a
statement about EMISSION, three stages upstream.

Anchors are file:line at the time of writing; they drift, the names do
not. Deeper treatments live in `docs/WorkerVertexCache.md` (emission
and adoption), `docs/SceneStreaming.md` (the element contract and the
tiers), `docs/IncrementalPublish.md` (delta), `docs/RenderEngine.md`
(the backend).

## The pipeline, in one line

    tessellate -> fill -> emission -> registration -> publish capture
                                          |               |
                                          +-> adoption ---+-> merge -> gate -> draw

Each stage below is defined by what it PRODUCES and where it runs; the
thread matters, because only value types cross between them.

## Core stages

**Tessellation** -- OCCT turning a `TopoShape` into triangles at some
deviation. Off-thread on the refine pool. Its output feeds the fill.

**Fill** -- building the Coin display arrays (`SoCoordinate3` points,
`SoNormal` vectors, the `coordIndex` arrays of the three drawable
nodes) from the tessellation. `fillVisualArrays`. Runs inline on the
GUI thread, or on a worker under the POOLED FILL.

**Pooled fill** -- the fill run on the refine pool instead of inline,
so a big rebuild does not stall the GUI. `queueVisualFillOnPool`,
`ViewProviderExt.cpp:5984`. GATED: it takes only landing-pump items
(`inLandingPump()`), never the load-time drain (`!s_drainVisualBuild`),
and only shapes at or above `Render_VisualFillMinFaces` (default
**2000**). That gate is why emission was dead until the inline path
learned to emit too -- it governs where the FILL runs, and emission
used to be wired inside the gated branch rather than alongside the
fill it mirrors.

**Emission** -- PRODUCING the plain-value mirror of what a traversal
capture would build: first-seen-order deduped vertices and normals,
plus triangle, line and point index lists
(`SoFCVertexCache::PrebuiltContent`). `emitVCacheCore`,
`ViewProviderExt.cpp:6860`. Value types only (`SbVec3f`, `int32_t`) --
no Coin object crosses a thread boundary, and `COIN_THREADSAFE` stays
off. Two producers: the pooled fill emits from the fill data on a
worker, and `updateVisual`'s epilogue emits from the FINAL node arrays
(`emitVisualVertexCacheFromNodes`) for every inline path. Emitting from
the nodes is the more robust of the two -- whatever is in them is by
construction what the publish will capture.

**Registration** -- the landing, back on the GUI thread, FILING emitted
content in a static registry keyed on the shape node:
`SoFCVertexCache::setPrebuilt`, called from
`registerPendingVisualVertexCache` (`ViewProviderExt.cpp:7009`). It
stamps `content->nodeid = node->getNodeId()` at that instant, and
deliberately AFTER the rebuild's last field write on the node
(appearance, highlighted edges and points). The stamp is what makes an
entry voidable.

**Restamp** -- moving a registered entry's stamp to the node's current
id, for a touch that changed no geometry. `SoFCVertexCache::restamp`.
An appearance apply invalidates VBOs and touches the shape node
(`SoBrepFaceSet::doAction` answers `SoUpdateVBOAction` with `touch()`),
which would otherwise void content that still mirrors the node exactly.
The caller asserts the geometry is unchanged; a rebuild, where it is
not, must never restamp.

**Publish** -- one pass of the render-cache manager over the changed
scene graph, producing the `SoFCRenderCache` tree the frame draws from.
`SoFCRenderCacheManager::render`. The unit that the capture budget,
the deferral machinery and the delta all reason about.

**Capture** -- within a publish, building one shape's
`SoFCVertexCache` by walking the primitives Coin generates
(`generatePrimitives` -> `addTriangle`/`addLine`/`addPoint`) and
deduping vertices in a hash. This is the expensive thing: measured at
about half of storm paint time.

**Adoption** -- a capture that INSTALLS registered content instead of
walking primitives: `takePrebuilt(node)` -> contract check
(`prebuiltReject()`) -> `installPrebuilt()` -> PRUNE, in
`SoFCRenderCacheManagerP::preShape`. One-shot: the registry entry is
consumed whether or not the install succeeds. An adopted shape is
NEVER deferred (see budget).

**Merge** -- flattening the render-cache tree into per-material draw
lists (`vcachemap`) for the frame, `SoFCRenderCache.cpp:2340+`. Note
that the incomplete mark is read HERE, not at capture time, so capture
order within a publish cannot lose it.

## The budget and deferral

**Capture budget** -- `Render_CaptureBudgetMS`, default 50ms, a
per-publish time allowance for traversal capture.

**Deferral** -- what a publish does with a changed shape once the
budget is spent AND at least one shape has been captured: the shape
keeps its previous cache as a STAND-IN and does not re-capture, or, if
it is first-time, stays out of the frame entirely. `preShape`,
`SoFCRenderCacheManager.cpp:2300`.

**Stand-in (`prev`)** -- the shape's previous vertex cache, re-added to
the frame in place of the deferred capture. Consequence worth naming: a
deferred re-capture leaves the drawable STALE, not ABSENT. Only a
first-time shape is truly absent.

**Monotonic drain** -- the rule that each publish captures at least one
shape before deferring anything, so a storm always makes progress.
Watch it in the log: `N deferred` falls to 0.

**Storm** -- the burst of publishes during a load or a mass edit, where
most shapes change at once and the budget defers most of them.

**Drain (load-time)** -- `s_drainVisualBuild`, the state locker around
the load-time visual build. Excluded from the pooled fill on purpose:
its giants already have stand-ins.

**Incomplete** -- set on every open ancestor cache when a shape defers,
so the next publish walks back down instead of pruning at an
ancestor that has turned valid.

**Incomplete here** -- the stronger mark, set from the deferred shape
up to its SELECTION ROOT and no further: entries merged through a
so-marked cache carry `VertexCacheEntry::incomplete` out to the
display, which is how a viewer learns the object is missing one of its
own drawables. `setIncompleteHere`, read at merge.

**Selection root** -- the cache where an object's sibling drawables all
pass through; above it belongs to other objects. The boundary the
incomplete-here mark stops at.

## Drawables and the element contract

**Drawable classes** -- each Part shape publishes up to three shape
nodes: `faceset` (`SoBrepFaceSet`), `lineset` (`SoBrepEdgeSet`),
`nodeset` (`SoBrepPointSet`). Each gets its own vertex cache keyed on
its node. Scene-graph order within an object is
**faceset -> lineset -> nodeset** (`ViewProviderExt.cpp:2300-2335`),
which is why a spent budget can only ever drop lines and points while
the faces stay in, never the reverse.

**Attached vs floating** -- an ATTACHED line set holds only edges that
bound a face; an ATTACHED point set only vertices that an edge ends at.
A FLOATING one holds geometry nothing attaches to -- a bare vertex, an
edge with no face. Floating sets rank WITH the faces and are never
gated: they are the object.

**Element contract** -- the dependency rule in `BGFXFrame.cpp:3755+`:
an attached line set draws only while its object's faces are shown; an
attached point set only while its lines are. Decided per frame,
dependency-ordered, faces first. On-top and highlight draws are never
gated.

**Coarse-faces half** -- the arm that holds an attached set while its
object's faces sit on a rough ladder rung (`levelError > 0`). MEASURED
firing on both tiers.

**Incompleteness half** -- the arm that holds an attached set when its
object has NO companion draw at all and the object is marked
incomplete, i.e. the companion is LATE rather than absent-by-display-
mode. Has never been observed firing: the ordering it needs (points or
lines present while faces are missing) cannot be produced by the
budget, only by adoption, and adoption never happens. See
`docs/WorkerVertexCache.md`.

**Pressure stages** -- the separate, memory-driven arm: under
`gpuOverBudget` the classes are spent points -> lines -> faces and
taken back in reverse, on a staged latch. Not armed on the standalone
streamed tier, whose budget is the CPU/resident half only. Do not read
a zero there as a failure.

## Ladder and landings

**Ladder** -- the progressive level-of-detail machinery: a shape is
first shown on a coarse RUNG and refined toward the exact tessellation
as budget allows.

**Rung / levelError** -- one step of the ladder and its error; 0 means
the exact tessellation.

**Descent** -- moving a shape to a finer rung, which re-tessellates
off-thread.

**Landing** -- an off-thread result arriving back on the GUI thread to
be installed.

**Landing pump** -- `pumpLandings()`, `MeshLevelSource.cpp:320`, the
GUI-thread turn that installs queued landings within
`Render_LevelLandBudgetMS`. `inLandingPump()` is true only inside it,
and it is the ONLY context in which the pooled fill -- and therefore
emission -- can run.

## The prebuilt contract

What adoption requires of the captured state, because the worker baked
neither colors nor texture coordinates. `prebuiltReject()` returns the
clause that refused, or null:

- `closed` -- called outside the open()/close() window.
- `prev attached` -- the cache is a partial-subset cache sharing a
  previous cache's indexer.
- `color per vertex` -- more than one diffuse or transparency value.
- `texture unit` -- any enabled texture unit (or forced UV capture).
- `bump coords` -- bump map coordinates present.
- `markers` -- marker indices on the point set.
- `glrender` / `flipped normal` -- capture modes the mirror does not
  reproduce.

**Verify arm** -- `Render_WorkerVertexCache = 2`: adopt AND run the
traversal capture, compare the arrays, keep the traversal result. The
mode that proved the emission byte-exact on landings.

**Proto node** -- a color variant of a shared tessellation names its
base shape node, so a fresh cache can be seeded from the base's cache
and keep the geometry arrays shared while only the baked color array
detaches.

## Reading the capture-budget line

    capture budget: 45 captured in 1ms, 500 deferred,
                    0 adopted of 545 offered, 545 no entry

- **captured** -- shapes this publish walked by traversal.
- **deferred** -- shapes it held back once the budget was spent.
- **offered** -- shapes that ASKED the registry for content, i.e. every
  shape reaching `takePrebuilt` with the feature on.
- **adopted** -- of those, the ones installed without a traversal walk.
- then the causes, which is the point of the line:
  - `no entry` -- nothing was ever registered. The failure is in
    EMISSION, upstream of everything the word "adoption" suggests.
  - `stale id` -- content was registered and the node was touched
    afterwards, voiding the stamp. The failure is in REGISTRATION
    ORDER.
  - anything else -- a `prebuiltReject()` clause; the state was outside
    the contract and the traversal capture ran, correctly.

The three are counted separately ON PURPOSE. A single zero at the end
of a pipeline names the last stage and hides which of them actually
failed; splitting them is what turned a two-session question into a
five-grep answer.
