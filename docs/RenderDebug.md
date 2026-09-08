# Render Debugging & Verification Architecture

Status: phases **1–4 implemented** (RenderDebug view properties + bgfx
buffer visualization/freeze-frame; `saveRenderDump`/`getRenderStats` +
sidecar JSON; browser `dumpFrame`/`reload` control channel; the
verification harness under `scripts/`; dynamic named uniform binding +
`u_userParams` pool + shader hot-reload). Phases 5+ are design.

This document defines (1) the governing policy for render-debugging code in this
repo, (2) the debug-parameter protocol that rides the existing render-property
plumbing, (3) the frame-capture Python API, and (4) how all of this is designed
as the first constrained slice of an eventual **user-loadable shader** feature.

Companion docs: `docs/RoadMap.md` (workstreams), `docs/TShapeRenderCache.md`
(renderer instancing), `docs/ThinClient.md` (browser control channel — the
same channel these debug knobs travel over).

---

## 1. Governing policy

The bgfx renderer runs on three targets — desktop GPU, streamed WASM/browser,
mobile browser — and its hardest bugs are target-specific (fp16 filtering,
missing WebGL extensions, driver-dependent precision). A debug probe that only
exists as a local patch on one target is nearly worthless. Hence:

1. **Debug output is a feature, not a patch.** No probe lands as
   working-tree-only code. If it is worth writing, it is worth a name, a
   runtime toggle, and a commit.
2. **Runtime toggle over recompile.** Every debug knob is a property or
   parameter, settable live. A `#define` cannot be flipped on a phone or in a
   streamed WASM session; a `RenderDebug_*` view property can, over the same
   channels the render properties already travel.
3. **Generalize the mechanism, not the instance.** Do not add
   "tint shadow cascade N red". Add "bind a named parameter to a shader lane",
   after which the cascade tint is a property value, not a C++ change.
4. **Capture through an API, never ad hoc.** Before writing any
   dump/readback code, look for an existing Python API. If none fits, add one
   (section 4) — the API outlives the bug.
5. **One knob surface, every backend.** A debug knob is defined once, on the
   shared config feed (section 2), so desktop and WASM honor the *same* knob.
   This is a hard requirement: cross-target pixel comparison is only
   meaningful when both legs were staged by identical, scriptable state.

Corollary for reviews: a PR that adds `getenv`-gated or `#if 0` render-debug
code should be reworked to a `RenderDebug_*` parameter unless there is a concrete
reason (e.g. code that must run before any parameter system exists).
Existing env gates (`FC_BGFX_DEBUG_READBACK`, `FC_BGFX_DEBUG_DUMP_FRAME`) are
grandfathered and get absorbed by section 4.

---

> **2026-08-14 revision: the fixed debug switches are GLOBAL parameters
> now, not per-view properties.** Everything this document describes as a
> `RenderDebug_<Switch>` view property -- ViewMode, FreezeFrame, Label,
> Timing, Delta, Coverage, Occlusion, ProxyCut, ProxyGen, CullAudit,
> CullBounds -- plus the occlusion-culling knobs (`Render_Occlusion*`),
> the ladder tuning knobs (GpuMemoryBudgetMB, LevelDebug,
> LevelCeilingSimulateMB, LevelPressureRelease, DowngradeLedger,
> ClimbHardLimit, ClimbAdmitBatch, DescentOrderBatch, ShapeVertices,
> PressureDropEdges, LoadDropElements) is read from the global
> RenderParams only (`Preferences/View/Render`; the RenderDebug ones as
> `Debug<Switch>`). The per-view copies were retired because documents
> SAVE view properties: a model file carrying a stale override silently
> shadowed whatever a measurement harness or the preferences set, and
> that burned multiple measurement runs. Old files still load; the dead
> properties are stripped on view restore
> (`Gui::stripLegacyRenderProperties`). What REMAINS per-view is the
> genuinely display-intent surface (the effects groups, CoarseTessellation,
> LevelTolerance) and the **custom named shader parameters** of section
> 2.5 -- any `RenderDebug_<name>` property outside the fixed list still
> feeds a like-named `u_<name>` uniform, per view, as before.

## 2. The debug-parameter protocol

### 2.1 The existing spine (already implemented)

Runtime render settings already flow through one pipeline:

```
Preferences/View/Render key            (global default, RenderParams — cog-generated)
        │  per-view override
        ▼
<Group>_<Name> dynamic property on the View3DInventor object
        │  viewPropOverride/viewParamOverride    (SoFCRendererBridge.cpp)
        ▼
RendererBridge::translate<Group>Config  → Render::<Group>Config struct
        │  per-frame                            (SoFCRenderer.cpp ~2287)
        ▼
Renderer::set<Group>Config virtual      → BGFXRenderer marks dirty
        │  frame packing
        ▼
bgfx::setUniform(u_*)                   → uniform vec4 u_* in the .sc shader
```

Two properties of this spine make it the right home for debug knobs:

- **The naming protocol is already in place.** `viewPropOverride` looks up
  properties by `"%s_%s" (group, name)`, and since f491c40a75,
  `ViewProviderGeometryObject::addDynamicProperty` derives the property-editor
  group from the name's prefix before the first `_`. So a dynamic property
  named `RenderDebug_ViewMode` automatically lands in group "RenderDebug" and is found by
  the bridge with no registration step. (Note: the prefix must be non-empty —
  a *leading* underscore is excluded — so the convention is `RenderDebug_Foo`,
  never `_RenderDebug_Foo`.)
- **Both backends consume the same feed.** The desktop GL and WASM builds run
  the same `SoFCRenderer` → `set*Config` path, so a knob works on every
  target the moment it exists, including over the thin-client control channel.

### 2.2 What we add

A `RenderDebug` group, structurally identical to the existing `AO`/`Water`/`Bloom`
groups:

- **No special UI.** Debug knobs are dynamic properties on the view object in
  the existing property editor, flagged `Hidden` so they only appear under
  "Show all" (or when added explicitly from Python). Global defaults live as
  plain keys in `Preferences/View/Render` (`RenderParams.py` cog source),
  reachable via Edit Parameters — no preference page, no task panel.
- `Render::RenderDebugConfig` struct in `Renderer.h` + `Renderer::setRenderDebugConfig()`
  virtual (default no-op, so the Diligent backend and any future backend
  compile unchanged).
- `RendererBridge::translateRenderDebugConfig()` reading each field through
  `viewParamOverride(view, "RenderDebug", ...)`.
- One `u_debugParams` (vec4) uniform in the bgfx backend, packed from the
  config each frame, declared in the shaders that participate (initially the
  final composite pass).

Setting a knob from Python is then one line, live, on any target:

```python
view = Gui.ActiveDocument.ActiveView
view.addProperty("App::PropertyInteger", "RenderDebug_ViewMode")  # group auto-derived
view.RenderDebug_ViewMode = 2   # world-space normals
```

(`MDIView.addProperty`/`removeProperty` — available on every MDI view,
mirroring the DocumentObject API; the editor group is derived from the
name prefix before the first `_`.)

### 2.3 `RenderDebug_ViewMode` — buffer visualization

The workhorse knob: a single integer that routes an intermediate render target
or derived quantity to the screen, instead of N per-artifact tint hacks. This
is the "view mode" dropdown every production engine ships.

| value | name | shows | diagnoses |
|---|---|---|---|
| 0 | Off | off (normal shading) | — |
| 1 | Depth | linearized depth | prepass, precision, far-plane issues |
| 2 | Normal | view-space normals (the prepass normal: no finish, no bump map; the Cycles path honours this mode too, with its shading normal, docs/CyclesIntegration.md sec 6.6) | tessellation/normal-generation bugs |
| 3 | AO | AO term only | GTAO artifacts, resolution-scaling seams |
| 4 | Shadow | shadow term only | acne/peter-panning, EVSM bleeding |
| 5 | ShadowTile | scene-shadow-map coverage (gray) + bulb atlas tile index as color | shadow projection reach, bulb-tile coverage/selection |
| 6 | Overdraw | overdraw heatmap (dedicated counting re-render, additive, depth test off) | transparency sorting, instancing regressions |
| 7 | ShadowFilter | shadow-moment filtering-precision probe: hardware bilinear vs the same four texels blended at full shader precision, amplified; B = the variance term | filtering-precision issues (exactly the RG16F stipple class) |
| 8 | UV | UV / texcoord of the visible surface (depth-tested re-render) | mapping bugs |
| 9 | Reflection | the planar reflection target, tinted dark red where the mirror covered nothing | mirror pass not running/stale, mirrored camera framing, what does and does not reach the mirror |
| 10 | ImpactMap | the particle impact map (docs/RenderEngine.md §5.8) stretched over the screen: green where a hit is recorded, brightness its age against the ring lifetime, blue its strength, dark red where nothing has ever struck | impact-driven water rings — separates "the step program reported nothing" from "reported in the wrong place" from "the surface fails to show what is there" |
| 11 | InstanceId | the identity of the draw that owns each pixel — every scene draw re-rasterized with the cull mask **ignored**, writing `drawIndex + 1` as an exact 24-bit integer (screen shows a hashed palette; the target holds the exact id) | which draws actually reach the screen — the ground truth behind `RenderDebug_CullAudit` (§2.4e), and the answer to "is this draw contributing anything at all" |

Implementation shape: the mode rides `u_debugParams.x`; the final composite
shader (`fs_fc_debug.sc`) ends in a mode `switch` that samples the relevant
intermediate target. Modes 1–5/7 are pure routing over targets that already
exist for the effect passes (modes 4/5/7 reconstruct the view-space position
from the prepass depth, so they force the prepass on like 1–3). Modes 6/8/11
share a dedicated *debug scene re-render* pass (`ViewDebugScene`, repurposing
the retired AO-apply view slot): every main-pass triangle fill re-rasterizes
into a full-res RGBA16F target — additive with the depth test off for the
fragment count, depth-tested texcoord output for UV, the draw's own identity
for the id mode. The enum is append-only.

**The Cycles path honours mode 2** (built 2026-08-29,
docs/CyclesIntegration.md sec 6.6) and renders normally for every other
value, since a path tracer has no prepass, AO target or shadow map to
route. `SceneInput::debugView` carries the value from the same
`RenderParams::getDebugViewMode()` read the bgfx frame makes, set by all
three feeders (the offline `cyclesRender`, the viewport cell, the served
backend), and the translator replaces every surface shader by an emission
of the view-space SHADING normal as `n * 0.5 + 0.5` -- the normal after
the bump map and the surface finish, which is where it differs from the
raster's mode 2: that one shows the geometry prepass normal, before
either. The offline file is written raw (no output transform) over black,
exactly as the raster composite writes the mode; the viewport blit goes
through the host frame, whose output transform is already off in any
debug mode. The one picture in which the two engines can be compared
exactly: unfinished shapes agree to 8-bit precision, and the finish and
map probes assert on it because lighting cannot serve -- the same 17
degree flank reads as a 9% luminance change in the raster and 1% in
Cycles (a nearly tilt-insensitive environment plus bump shadowing).
Changing the mode under a running Cycles session re-keys every shader
and so rebuilds the scene: a reset, not a device re-init.

#### 2.3b Mode 11 in detail — the id image is a measuring instrument

It is not a visualization that happens to be readable; it is ground truth,
and three of its properties are load-bearing:

- **Per instance, not per mesh.** The id is the `DrawCall` row, which is
  exactly what the occlusion cull mask is indexed by
  (`ProxyInstance::drawIndex`). Any coarser and a disagreement would name
  something that cannot be masked.
- ⭐⭐ **Exact integers, never a hash or a palette.** Two draws sharing a
  value is precisely the failure the instrument exists to detect. The
  target is RGBA16F, which carries 0..255 per channel exactly; the id is
  split into three raw byte lanes. What the *screen* shows is a hashed
  palette (consecutive ids differ by one and would otherwise be an
  invisible gradient) — the screen is never what the audit reads.
- **Every geometry kind, and the cull mask ignored.** Lines and points go
  through the same screen-space quad expansion the beauty pass uses, so
  the coverage is the coverage; the residual damage of occlusion culling
  shows up on edges, and an audit that skipped them would come back clean
  while missing exactly the draws that were wrong. Ignoring the mask is
  the whole point: the image has to be what the frame *would* have drawn
  had nothing been skipped.

⚠️ Coincident geometry is resolved by draw order (the view is Sequential,
on-top draws submitted last), where the beauty pass would state-sort. Two
draws at identical depth may therefore swap owners. This has not been
observed to matter — see §12.9's validation row, where 818 masked
instances produced zero false reports — but it is the first thing to
suspect if the audit ever names something the picture cannot corroborate.

### 2.4e `RenderDebug_CullAudit` — what the culling actually deleted

Turns the id image into a check on the occlusion culling
(docs/FarFieldProxies.md §12.9): the image is read back once a second and
intersected with the cull mask *as it was when that image was rendered*.

- `visible(id) ∩ masked` is a set of **proven over-culls** — named draws,
  each with a pixel count, ranked. Not "N pixels differ between two
  pictures", which is a symptom that names nothing.
- The same histogram gives the converse for free: rows that were drawn and
  own **no pixel at all**, which is the headroom the culling has not taken.

Independent of `ViewMode` — measuring what a frame skipped and looking at a
false-colour id image are different jobs, and the audit must not require
the screen to show something nobody can navigate by.

⛔ **The instrument validates itself before it is believed.** An id pass
that drew nothing reports a flawless culling and a colossal amount of
wasted work — the most convincing possible output, and entirely a report
that the instrument is broken. So a frame whose id image covers no pixel
at all refuses to compute and says so, the same shape as
`OcclusionFrameStats::rootRefused`. The snapshots are load-bearing for the
same reason: a readback lands a frame or two late, and checking it against
the *then-current* mask would reintroduce, inside the instrument, the very
one-frame skew it was built to find.

Needs a backend with texture readback, which WebGL2 is not; it says so once
rather than reporting zeros. The id image itself still renders there.

#### ⭐⭐ The attribution line — which verdict deleted the row

A second line, printed only when there is over-cull to explain
(docs/FarFieldProxies.md §12.10). The audit line names the *draws* the
culling wrongly removed; this one names the **verdict** that removed each,
and says how fresh and how stable that verdict was:

```
render cull attribution: N node(s) account for P px
  | verdict fresh (<=2f) A px (x%), older B px (y%)
  | from nodes that have flipped >=3 times: C px (z%)
  | frustum, not occlusion: R rows S px
  | worst n<id>(L<level> res<n> sub<n> lastpx<n> age<n>f hid<n>f ev<n>):<n>px/<n>rows
```

- **`frustum, not occlusion`** separates rows the occlusion culler cut from
  rows something else in the same mask cut. They are different bugs in
  different code, and a fix credited to the wrong one is worse than no fix.
  The percentages above it are shares of what *occlusion* cut, so that the
  other bug getting worse cannot silently shrink them.
- **fresh vs older** is the age of the answer that produced the verdict. A
  query that lied and a correct answer the world moved out from under look
  identical in one verdict; against a static camera, a *fresh* verdict had no
  time for the second explanation.
- `ev` counts how many times the node has **entered** the hidden state (not
  how often it was re-confirmed), and `hid` how long it has been there: ⭐
  together they distinguish a stable wrong verdict (`ev1`, large `hid`) from
  an oscillator (`ev` climbing) without needing a second frame to compare
  against.

⛔ **What this line deliberately does not report**, because it would be a
tautology: whether the deciding test was taken while the node's own contents
were being drawn. Only an already-hidden node is offered a test from the
hidden set, so the answer that first hides a node is *always* of the drawn
kind — the number would read 100% for every scene, including every scene
where the explanation it appears to support is false. See
`OcclusionNodeState` and docs/FarFieldProxies.md §12.10.

The node states are snapshotted with the image, like the mask, for the
reason given above.

#### The companion `render culling:` line — and its query accounting

Printed on the `RenderDebug_Timing` cadence whenever occlusion culling is
on, because "the audit found nothing" and "the mechanism never ran" are
otherwise the same output. Three of its fields exist to keep the *tests*
honest, separately from the verdicts:

```
| tests offered N budgeted M sent S | queries inflight I held H expired E refused R |
```

- **offered / budgeted / sent** are three different numbers and the gap
  between them is diagnostic: the walk *offers* every node it wants tested,
  `budget` caps what one frame may ask, and `sent` is what the backend
  actually drew boxes for. A transient buffer that ran short shows up only
  in the last one.
- ⚠️⚠️ **`inflight` / `held` / `expired` / `refused`** are the occlusion
  query leases (docs/FarFieldProxies.md §12.11). A bgfx query handle is an
  object's identity, not a slot to rent — reuse it and the next test reads
  the previous one's verdict, with no assert and no `NoResult` to catch it —
  so each test gets a handle of its own. `held` runs ahead of `inflight`
  because bgfx does not free a released handle until the frame ends;
  `refused` counts tests dropped for want of a handle (costs culling, never
  correctness) and `expired` counts queries that never answered at all.
  Non-zero `refused` means the backend's pool is too small for the budget.

The software oracle prints a different right-hand half — it has no
queries to account for — carrying the granularity the question was asked
at (§12.17) and the coarse occluder hulls (§12.16):

```
| perinst tested N hid K redundant R in Z ms
| hulls C of D draws, saved T tris (held H, built B, pending P, X MB, Y ms)
```

- **hid** is the whole of what per-instance testing buys: draws whose own
  box is covered, sitting in a group that had already answered visible.
  Read it against `instances hidden` on the left — measured, it is 17% of
  the total on the benchmark, for 0.4 ms.
- **redundant** counts nodes holding one instance and nothing below them,
  whose content box *is* that instance's box, so the answer was already
  taken. ⚠️ A partition fine enough to make every leaf a single instance
  drives `tested` to zero and `redundant` to everything — the node walk is
  then already per-instance and there is nothing left to ask. That is a
  correct reading, not a broken one, and it is what a unit test with
  `maxPerCell = 1` measures.

- **C of D** is how much of the pass ran on hulls rather than on meshes,
  and **saved** is the budget those draws did not spend — which is the
  budget that went instead to a candidate the triangle cap would
  otherwise have dropped.
- ⚠️ **`pending`** is the one to read before anything else in a coarse
  row. Hulls are built a few per frame, so a scene that has just come
  into view rasterizes meshes for its first frames; a measurement taken
  while `pending` is non-zero is a measurement of the warm-up, and it
  reads as a weak version of the mechanism rather than as an unfinished
  one. `cull_audit.py` prints it beside every coarse row for that reason.

### 2.4f `RenderDebug_CullBounds` — would a tighter occludee bound pay?

A **diagnostic that decides whether a mechanism is worth building**, not a
mechanism (docs/FarFieldProxies.md §12.19). It rides the cull audit's
frame and asks every still-drawn row three more times against the same
occluder buffer — with the world box that ships, with the mesh's own box
through its model matrix, and with every triangle and line segment asked
separately. **Nothing is culled by any of it**: the verdicts are counted
against the id image and discarded.

```
render tight-bound audit: D drawn, N invisible (J of them judged)
  | control world AABB cull C (p prize + r RISK) = rows never asked
  | OBB corners cull O (...), o over control
  | per-primitive cull P (...), p over control
  | ceiling X% of judged invisible | judged J skipped S (points, no mesh) ...
```

Read it in this order, because two of the columns exist to stop the other
ones being believed too early:

- ⚠️⚠️ **RISK** — rows an arm would cull that own pixels. It must be zero.
  An arm is conservative on paper until the id image has been asked, and
  this workstream twice shipped a box test that answered hidden for
  things plainly on screen (§12.6, §12.10).
- ⚠️ **control** — the shipping world box, re-run here. A row it culls was
  never asked by the pass at all, so it is a *coverage* gap and belongs to
  neither tighter arm. Non-zero control means the arms' totals are
  measuring the wrong thing.
- ⚠️⚠️ **`(J of them judged)`** is the ceiling's denominator, and it is not
  N. The first run of this diagnostic asked only about triangles, judged
  2803 of 8388 rows, divided by all 7574 invisible ones and reported a
  mechanism as weak that had never been offered two thirds of the
  problem — a CAD frame draws each object's edges as well as its faces.
  Line draws are now judged; **point draws still are not, on purpose**: a
  point is a sprite and its vertex is not its footprint, so it is the one
  arm here that could answer hidden for something visible.

The arms are monotone — a triangle's hull lies inside the OBB, whose hull
lies inside the AABB — so an arm that adds nothing to its predecessor is
a proven dead end rather than an unlucky sample. Measured, the OBB arm
adds 8 rows and the per-primitive ceiling 457 of 5157, which is what
closed the occludee-bound question (§12.19).

⚠️ **It costs far more than a frame** (~2M primitive queries, 27-38 ms)
and runs only on the audit's frame. A row measured with it on is not
comparable for timings with a row measured without it; `cull_audit.py`
gates it behind `FC_TIGHT=1` and says so. Needs the cull audit on (it
supplies the image) and the software occluder pass (it owns the buffer
being re-asked).

Mode-specific tuning rides the `u_userParams[0]` bootstrap lane: `.z`
overrides the overdraw full-red count (default 8) and the mode-7 probe
amplification (default 4096); `.x/.y` stay the generic output scale/bias.
(This renderer has a single scene shadow map — no cascades — so mode 5's
"cascade index" reduces to scene-map coverage plus the 4x4 bulb tile atlas.)

**Why this matters for verification:** a golden-image diff of the final frame
says "something changed". A diff of mode 2 vs mode 3 vs mode 4 *localizes* the
regression to a pipeline stage. The verification harness (section 5) captures
a small set of modes per scripted camera, not just the beauty shot.

### 2.4 `RenderDebug_FreezeFrame` — determinism switch

A boolean that disables every intentionally non-deterministic input:
temporal jitter/accumulation (AO frame cache), time-driven animation (water,
fire), and any per-frame random sampling. Without it, golden-image comparison
flakes and every pixel-diff threshold becomes a negotiation. With it, two
frames of the same scene+camera+params are bit-stable per backend.

This is also the knob the capture API (section 4) sets implicitly when asked
for a "verification capture".

### 2.4b `RenderDebug_Coverage` — what the camera can resolve

A boolean that logs, once a second, a histogram of how many pixels each
drawn object covers: its projected bounding-box diagonal, bucketed
(`<=1px`, `<=4`, `<=16`, `<=64`, `<=256`, above), with on-screen,
off-screen and no-bounds counts and the share of on-screen objects at or
under 4px.

It answers the measurement `docs/FarFieldProxies.md` §9 gates that
workstream on: a part costs a whole object — a cache entry, a draw entry,
a material, an identity — whether it fills the screen or four pixels of
it, so what decides whether aggregating distant parts pays is how much of
the model a normal camera cannot resolve.

The projection is `PlanBoxes::sight()`, the same one the level plan ranks
with, so the histogram and the refine pass agree by construction about
what "small on screen" means. It is computed per *object*, not per draw —
a part drawing several times (opaque and transparent, faces and lines)
costs one object's worth of the overhead in question. Reported on the
level planner's schedule rather than per frame, since it is a property of
where the camera settled.

### 2.4c `RenderDebug_ProxyCut` — what a far-field cut would cost

A boolean that logs, once a second, what aggregating distant parts *would*
buy this camera — with nothing generated. The drawn instances are
partitioned into the spatial index of `docs/FarFieldProxies.md` §3, a
frontier is descended at 1, 4, 16 and 64 pixels, and each tolerance
reports the draws that cut would issue: one per (cell, material) proxy
(§5.1) plus whatever stays exact. A second line gives the per-level
distributions — nodes, residents, largest subtree, mean material buckets —
which are what size `K` and the extent target.

It is the gate of §11.1: if that draw count is not far below the draws
issued today, generating proxies is not worth building.

Two differences from `RenderDebug_Coverage` above are deliberate. It
counts **instances, not objects** — a part drawing three times pays three
draw entries, and instances are what a cut partitions. And it rebuilds the
partition on every report rather than caching it, because a measurement
that can go stale measures the wrong thing; the build cost is reported
rather than hidden, since the plan pass of phase 3 has to pay it too.

### 2.4d `RenderDebug_ProxyGen` — what a far-field proxy commits

The switch above estimates; this one generates. For a sample of the nodes
the 64px cut stops on it merges each (cell, material) group for real and
decimates it at the node's cell divided by 4, 8 and 16, then reports per
grid: the error committed as a fraction of the node's extent, the triangle
count against both the source and what instancing already shares, the
surface area retained, and how many members came back empty.

The error against the extent is the point of it. The cut estimate descends
by a node's projected *extent* because phase 1 had no proxy to have an
error, and the conversion between the two decides whether its table reads
as its 16px row or its 64px row (`docs/FarFieldProxies.md` §11.1b). The
answer, measured, is that no single ratio converts it — §11.1c.

⚠️ **Unlike every other switch here, this one builds meshes.** A report
merges up to two million triangles and decimates them three times, and the
frame it lands on stalls for as long as that takes. It is therefore
bounded — a fixed number of sampled nodes and a triangle budget — and it
reports what it skipped, since a measurement that silently drops most of
its work reads as coverage it did not have. Its rate limiter also measures
from when the last report *finished*, or a report costing more than the
interval would be due again the moment it returned.

Note these, `RenderDebug_Timing` and `RenderDebug_Delta` are all
measurement switches, not shader inputs, so §2.5's dynamic-uniform binding
skips them — otherwise each would upload a `vec4` uniform nothing
declares.

### 2.5 Generic named parameters — dynamic, not pre-declared

Debug shading frequently needs a couple of tweakable values (a bias to
sweep, a threshold to bisect, a channel selector). These are **not**
restricted to a pre-declared set, because nothing in the stack requires it:

- `bgfx::createUniform(name, type)` is fully dynamic at runtime — uniforms
  are resolved by name. The only hard rule is that a *compiled* shader
  binary has exactly the uniforms its source declared; you cannot inject a
  new uniform into an existing `.bin`. Since shaders are compiled on demand
  (§3 hot-reload, §6.3 compile cache), declaring a uniform is just an edit
  to the shader source.
- The C++ side is data-driven, not per-uniform code (implemented): the
  bridge enumerates every `RenderDebug_*` **and `RenderShadow_*`** view
  property beyond the fixed knobs of its group into
  `RenderDebugConfig::userParams` -- the uniform name is
  `"u_" + <Name>` (or `<Name>` verbatim when it already starts with
  `u_`), so `RenderDebug_myKnob` feeds `uniform vec4 u_myKnob`. The
  fixed knobs of each group are excluded because the engine reads them
  itself and each would otherwise upload a uniform nobody declares:
  the `RenderDebug` switches listed above, and
  `Gui::shadowRenderPropertyNames()` for the shadow map and its ground.
  A new prefixed group joins the rule by adding its exclusion list.
  Supported property types: Bool/Integer/Enumeration/Float → the x
  lane; Color → rgba; Vector → xyz; Float/IntegerList → consecutive
  lanes, zero-padded to vec4 arrays. Unsupported types warn once and
  are skipped. The backend resolves names to handles lazily
  (`setUserUniform`) and pushes the values against the debug-pass draw.
  Adding a knob is therefore: declare `uniform vec4 u_myKnob;` in the
  shader + add a like-named property. No FreeCAD recompile, no
  registration step.
- Residual constraint (bgfx, not us): uniform types are vec4 / matrix /
  sampler — scalars pack into vec4 lanes regardless of mechanism.
- **Binding is per-draw, not per-frame** (bgfx reality, learned the hard
  way): uniform updates recorded before an *empty* submit —
  `bgfx::touch`, i.e. the view clears — are discarded together with the
  dropped draw, so a frame-start "set everything once" push silently
  never reaches the GPU. Values must be set against a real draw that
  precedes (or is) their consumer. The stock push therefore lives with
  the debug-pass submit; a user-shader stage (§6) sets its parameters
  when binding its own pass.

**Bootstrap fallback (implemented).** One regime cannot compile on
demand: a target running stock precompiled binaries with no compile
service (the browser tier until §6.3's server-side compile lands). For
that case only, the stock debug-capable shaders reserve a small
`uniform vec4 u_userParams[4]` pool that properties map onto by lane —
a `RenderDebug_userParams` float-list property fills it (16 lanes),
riding the same dynamic binding. The debug composite shader applies
lane 0 as an output transform (`x` = scale, `y` = bias, backend
default 1/0), which amplifies subtle differences in captured debug
buffers. The pool value travels in the scene snapshot (v21), so a
stock WASM viewer honors it. It is a compatibility floor, not the
architecture — once every tier can reach a compiler, the pool is just
another set of named uniforms.

This dynamic binding is exactly how the user-shader feature (§6) binds
property-driven values too, which is why it lands under the protocol and
not as a debug-only hack.

---

## 3. Shader hot-reload (developer loop)

Editing a `.sc` shader used to mean recompile + restart. Implemented:

- **Shaders are distributed by source**: the repository carries only
  `.sc`/`.sh`; every `.bin` is a build artifact. The desktop build
  compiles all profiles with the in-tree `shaderc`
  (`BGFXShaders.cmake` → `ninja Renderer_assets`, incremental per
  shader) into the build tree's resource path; the WASM viewer build
  compiles+packs its own essl set with a host `shaderc`
  (`FCVIEWER_SHADERC`). No committed binaries, no stale-copy step.
- `FC_BGFX_SHADER_DIR=<dir>` points the shader loads at an alternate
  asset root (a directory containing `shaders/{glsl,essl,spirv}/`) —
  e.g. `bgfx/shaders/compile.sh`'s output (default
  `build/shaders-dev`), for A/B shader experiments that must not touch
  the build tree.
- `View3DInventor.reloadShaders()` reloads every program from disk on
  the next rendered frame (a shader-generation bump forces the view
  re-init that already owns program lifetime). The loop is:
  edit `.sc` → `ninja Renderer_assets` (or `compile.sh` into the
  override root) → `view.reloadShaders()` — no restart, verified
  byte-exact reversible.
- Desktop-only for now (shaderc is a host tool); the WASM story arrives
  with the user-shader feature's server-side compile step (section 6.3).
  A file watcher on the override dir could remove the explicit reload
  call later. True runtime JIT of the built-ins (ship source + shaderc
  in the distribution, compile on demand through the §6.3 cache) is a
  phase-6 option on top of this — the build-time compile stays as the
  fallback and the commit-time error gate either way.

This converts "hack the shader to print a color" from a rebuild cycle into an
edit-save-see loop, while keeping the shader *source* the artifact — which is
also the groundwork for loading user shader source at runtime.

---

## 4. Frame capture: a real Python API

### 4.1 What exists

- `View3DInventor.saveImage(...)` → `View3DInventorViewer::savePicture()`.
  When an external renderer is active it already forces the
  framebuffer-capture path, so the composited (Coin + bgfx) frame is
  capturable from Python today.
- `FC_BGFX_DEBUG_READBACK` / `FC_BGFX_DEBUG_DUMP_FRAME=<path>`
  (BGFXRenderer.cpp ~5704): a true desktop-GL readback of the bgfx color FBO
  (pre-Coin-composite), written as a PPM, overwritten every frame, env-gated.
  This was built during the stipple hunt precisely because streamed-viewer
  screenshots render on SwiftShader and cannot show real-GPU artifacts.

The env-gated dump is exactly the capability the verification harness needs,
in exactly the wrong form (no scripting, no format choice, no metadata,
always-on-per-frame).

### 4.2 What we add: `saveRenderDump`

```python
view.saveRenderDump(path,
                    source="renderer",    # "renderer" = bgfx FBO readback (pre-composite)
                                          # "framebuffer" = composited frame (savePicture path)
                    mode=None,            # optional RenderDebug_ViewMode for this capture only
                    metadata=True)        # write sidecar JSON
```

- One-shot: arms a capture that executes on the next rendered frame, then
  disarms. No env vars, no per-frame overwrite.
- **Consumed only by a complete frame.** The backend declines to consume
  the dump with a frame that is not yet the picture asked for, and holds
  it for a later one: a frame in which a draw asked for a user-shader
  program still compiling (`shaderc` runs in a subprocess; the stock
  program stands in meanwhile -- a surface without its material), or a
  frame drawn from a publish that deferred shapes under its capture
  budget (`Render CaptureBudgetMS`; the viewer says so through
  `Renderer::holdFrameDump()` before the frame). `frameDumpHeld()`
  reports a held frame, and `View3DInventorViewer::pumpFrameDump`
  counts it as progress: its quiet timeout (5 s) restarts on every held
  frame and every pending compile, under a 120 s total. Both holds are
  bounded by what they wait for -- a failed or watchdog-killed compile
  is recorded and its draw stands in for good without asking again, and
  each follow-up publish captures at least one more deferred shape. The
  case that made this necessary is section 5.2a.
- **The same verdict is a signal in its own right.** `Renderer::
  frameComplete()` is the last frame's verdict; `renderedFrames()` and
  `completeFrames()` count frames since the backend came up, so a
  waiter records the complete count and stops when it advances (a
  verdict left over from before the request proves nothing). What a
  complete frame means: every user-shader program compiled, every
  deferred shape arrived, a frozen frame's particle warm-up reached
  (`stepParticles` still owing steps under `DebugFreezeFrame`), and no
  mesh refine the level plan just asked for. On the frame path this is
  two flag writes, two increments and a compare -- nothing waits there.
  The consumers: `View3DInventorViewer::waitFrameComplete(timeoutMs)`
  pumps frames until one complete frame has rendered since the call
  (quiet 5 s restarting on every frame rendered and every pending
  compile, 120 s in all), the Qt signal `frameCompleted()` fires from
  `renderScene` on the frame that advanced the count, and Python has
  `view.waitFrameComplete(timeout=120000)` and `view.isFrameComplete()`.
  `render_verify.py` settles on the wait instead of a frame count
  (`RV_SETTLE` is now extra frames, default 0), which is what lets the
  chess set take exactly the time it needs and the small scene almost
  none. Cycles too: `view.cyclesRender()` waits for a complete frame
  before it translates the render cache (a publish still catching up
  deferred shapes would otherwise trace half a scene), and
  `view.cyclesViewportStatus()['complete']` says the live session has
  rendered its whole sample budget for the scene and camera as last
  stated -- what a probe polls instead of sleeping.
- **The wait is the default of every capture, and an argument.**
  `FrameDumpRequest::waitComplete` (default true) is what the frame
  tail honours; `saveRenderDump`, `saveImage`, `getRenderStats` and
  `cyclesRender` take `wait=True` and pass it down (`savePicture` /
  `imageFromRenderer` carry it in C++, so a Std_ViewScreenShot waits
  too). `wait=False` takes the very next frame as it stands, mid-arrival
  included -- the one thing a probe of the arrival itself needs.
- `source="renderer"` reuses the existing readback code path, promoted from
  env-gated static to a renderer-level `requestFrameDump(path, mode)` API;
  PNG via Qt's imagewriter instead of hand-rolled PPM.
- `mode=` temporarily overrides `RenderDebug_ViewMode` for the captured frame, so a
  harness can sweep buffer visualizations without touching view state.
- **Sidecar metadata** (`<path>.json`): camera (position/orientation/type/
  scale), viewport size, backend type + renderer caps line, MSAA samples,
  git describe, the full set of active `Render_*`/`RenderDebug_*` values,
  and the **preferences those do not cover** (see below).
  A capture is thereby *reproducible*: the harness (or a human) can re-stage
  the exact frame from the sidecar alone. "I saw stipple once" becomes a
  checked-in test case.
- **Why the preferences too.** A `Render_*` view property outranks the
  parameter it was seeded from -- `_renderParam` materializes it once
  when the renderer is selected and owns it after -- so the whole
  `View/Render` group is covered by the property set above,
  enumerations included (`AOMethod`, `MatcapPreset`, `WaterRippleType`
  are materialized by hand beside the `_renderParam` calls, because the
  generic helper cannot install the enum strings first).

  What the properties do **not** cover is the viewer's own rig, which
  has no property form at all: the lights, the scene ambient, the
  background, the chrome that adds pixels to a capture, and the
  `RenderCache` mode that decides whether a backend draws the frame in
  the first place. Nor a view that never selected a renderer, which has
  no `Render_*` properties to record. So the sidecar carries a
  `preferences` object: the look-affecting subset of `View`, plus the
  whole `View/Render` group beside it -- a parameter read against its
  property is what says whether the property was merely seeded from it
  or has since been overridden. Without this a sidecar can describe a
  capture faithfully and still re-stage into a different picture, which
  is how a stray scene-wide setting once passed for a renderer bug
  (`docs/RenderEngine.md` 7).

  ⚠️ Recording is not licence to write. Anything with a view property
  is restaged **through the property**, never by moving the user's
  global preference: the property is the per-view override that exists
  for exactly this, and a preference write outlives the document and
  the session. The verification harness is the one exception, and only
  because it runs against a private throwaway config.

  What goes out is what the config has **set**; a parameter still on its
  built-in default does not appear, because that default lives in the
  reading code, not in the group. Restaging into a fresh config -- what
  `render-verify.sh` does -- is therefore exact, and restaging into a
  config that has set a key this one left alone is best-effort.

  Values are bucketed by parameter **type** (`bool`, `int`, `unsigned`,
  `float`, `string`) rather than written flat, because a group is a set
  of typed maps and JSON cannot tell an int from an unsigned. The two
  are not interchangeable: `SetInt` on a key files it under `Integer`
  while `GetUnsigned` goes on reading the untouched `Unsigned` entry
  (measured -- `SetInt("BackgroundColor", 287454020)` then
  `GetUnsigned` reads 0). Guessing the setter from the JSON value would
  therefore replay every colour below `0x80000000` into the wrong slot
  and leave the frame with its old background.

`FC_BGFX_DEBUG_READBACK`'s stats (geometry-pixel count, average color) get the
same treatment as a `getRenderStats()` Python call — cheap numeric
assertions ("the scene is not black", "N pixels covered") are often enough
for smoke tests and are far more robust than pixel diffs.

### 4.3 Self-labeling captures

An optional burn-in of the active view-mode name + key parameter values into
a screen corner, so a PNG in a bug report is self-describing even without its
sidecar. Implemented as the Hidden `RenderDebug_Label` boolean (default off —
goldens stay label-free unless asked): while on, the viewer feeds an
orange monospace text quad (the fps-readout machinery, one percent in from
the top-left) through the standard overlay feed — the WASM viewer therefore
burns the same label into its own `dumpFrame` captures. The text names the
view mode, the freeze state, and every custom `RenderDebug_*` parameter as
`name=value`.

### 4.4 Live capture from a running browser (the third leg)

The WASM viewer runs *our* code on the *real device GPU* — the one place
server-side screenshots (SwiftShader) are constitutionally blind. So the
browser leg is the same one-shot dump protocol, transported over the
existing WS control channel:

1. Harness/Python sends `{cmd: "dumpFrame", mode: …, id: …}` as a JSON text
   frame (the same control channel the thin-client design uses).
2. The viewer arms a one-shot capture: on the next frame, after bgfx
   submits, it reads back the resolved color target via `glReadPixels`
   inside WASM — real WebGL2 on the user's actual GPU, desktop browser or
   phone.
3. Pixels + metadata (canvas size, echo of active `RenderDebug_*` state,
   `WEBGL_debug_renderer_info` string, snapshot version) return as a binary
   WS frame tagged with the request `id`; the receiving side writes the same
   PNG + sidecar JSON format as the desktop leg.

Consequences of this design:

- **No puppeteer required for capture.** It works against a phone
  mid-session — something no browser-automation screenshot can do.
  Puppeteer remains the *driver* in CI (open page, navigate); pixels come
  from the in-page readback, not `page.screenshot()`, which captures
  OS-composited output and can diverge from the GL result.
- **Phone convenience without the harness:** a HUD control triggering the
  same capture path and offering the image as a download — one extra
  button on an identical code path.
- `saveRenderDump` semantics become uniform across all three legs
  (desktop FBO readback / browser in-page readback / composited
  framebuffer), all landing as interchangeable PNG+JSON pairs.

**Unattended operation (agent/CI debug loop).** The capture is fully
drivable from the backend side with no human at the browser:
`saveRenderDump(path, source="viewer")` on the backend pushes `dumpFrame`
to the connected viewer(s), receives the upload, writes PNG + sidecar to
local disk, and returns the path(s). An agent (via the MCP debug console)
or a CI script therefore runs the whole loop — trigger, retrieve, inspect —
against whatever device is connected, including a phone left on the page.
If several viewers are connected, all are captured; filenames are
disambiguated by the sidecar's renderer-info string (one trigger →
per-device evidence). If no viewer is connected, the harness can launch a
local browser itself — noting that a *headless* browser renders on
SwiftShader, so it validates function/layout but not device-GPU precision;
real-GPU evidence comes from the desktop leg or a real connected device.

**Remote reload.** The same channel lets the backend refresh the viewer
itself when the served build changes:

- `{cmd: "reload", cacheBust: "<build-hash>"}` — the viewer re-navigates
  with the hash as a query parameter (not a bare `location.reload()`,
  which may reuse a stale cached `.wasm`/`.js` bundle).
- Steady state: the connect handshake carries the viewer's build/snapshot
  version; on mismatch with what the backend currently serves, the backend
  replies with `reload` automatically. Rebuild → every connected device
  (including a phone) refreshes itself on its next message, no commands
  from anyone. The viewer refuses a second reload for the same version
  string, so a bad build can't cause a reload loop.

---

## 5. The verification harness (consumer of all of the above)

The harness itself is a follow-up work item, but it is the reason for the
shape of everything above, so its contract is stated here:

- **Two legs, identical staging.** Desktop leg: real-GPU build,
  `saveRenderDump(source="renderer")`. Browser leg: streamed/WASM build, the
  same scene + camera + `RenderDebug_*` values sent over the thin-client control
  channel, pixels captured in-page via the `dumpFrame` protocol (§4.4).
- **Scripted cameras + `RenderDebug_FreezeFrame` on.** Each test scene defines a
  fixed camera list; every (scene, camera) captures the beauty shot plus a
  short list of `RenderDebug_ViewMode` buffers.
- **Diffs are per-stage.** Regressions are reported against the first
  pipeline stage whose buffer diverges, not just the final image.
- **Sidecars are the test manifest.** A stored golden is its PNG + JSON; the
  harness re-stages from the JSON, so goldens survive default-value changes.
- ⚠️ **Bless goldens from a restaged capture, not from a fresh one.** The
  sidecar stores the camera as Coin ASCII, which keeps about 8 significant
  digits, so a restaged camera is never bit-identical to the freshly staged one
  it came from -- `orientation` and `nearDistance` differ in their last digit or
  two. Most stages absorb that, but `ViewMode` 7 (the shadow-moment
  filtering-precision probe) is threshold-banded and flips ~0.3% of its pixels
  (max delta 147), which reads as a regression forever after. Restage-vs-restage
  is byte-exact because both sides then share the same already-rounded camera,
  and the rounding is idempotent after one pass. So: capture once, re-capture
  with `--golden` pointing at that first set, and bless *the second* directory.
- ⚠️ **Always pass the full `--modes` list.** The default is `0,1,2,3,4`; a
  golden set holding 0-8 then compares only five stages and prints the rest as a
  `modes only on one side` *note*, not a failure -- a silently partial pass.
- ⚠️ **A lone `beauty DIVERGED` with every other stage at 0.0000% is the
  harness, not the renderer.** Mode 0 is the first capture after a camera is
  restaged, and that slot can catch a frame the scene has not been drawn into
  yet: the beauty shot comes back as background plus chrome while the depth,
  normal, AO and shadow buffers captured a few hundred ms later are
  byte-identical to the golden. A missing model that leaves an *identical
  depth buffer* is a contradiction, and that contradiction is the tell -- read
  it as a flake and re-run before hunting a cause. Seen once in ~6
  `demo-fountain` runs, on one camera. The same first-frame settling shows up
  as ~20 differing pixels between two captures with nothing done between them,
  which is why a round-trip test needs a no-op control leg to measure its own
  noise floor rather than comparing against zero.
- ⚠️ **`--gpu` needs a real Wayland socket.** From a shell without
  `/run/user/$(id -u)` (agent sessions), set
  `XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir` or Qt finds no platform plugin and the
  capture aborts.
- ⚠️ **Set the camera, never ask for it.** `viewIsometric()` and friends
  *animate*. A grab a few `updateGui()` calls later catches the camera
  mid-flight, and the failure does not look like a camera fault: a
  near-horizontal orthographic camera cuts a hard horizon across the
  frame, because a horizontal plane seen edge-on covers exactly the
  lower half of an orthographic view. That reads as a half-sized ground
  plane, and it cost a session's worth of suspicion of the backend's
  shadow ground before the two legs were shown to agree to within 0.2%.
  Assign `cam.orientation` the literal rotation, and re-pin immediately
  before the grab — entering a draw style can move the camera again.
- ⚠️ **Set a draw style by property, not by `runCommand`.**
  `Gui.runCommand("Std_DrawStyleShadow", 0)` on a viewer *already* in
  shadow mode is the light-manipulator toggle, not a no-op, so the
  second leg of an A/B raises the dragger — which draws, and which
  `View3DInventorViewer::Private::getBoundingBox` folds into the scene
  bounds, moving everything sized from them. `view.DrawStyle = "Shadow"`
  is idempotent.
- WARNING: **`saveImage` is blind to anything that renders into its own
  FBO, and to the composite.** It re-renders offscreen, so a nested
  render target is flattened away and Coin's on-top pass never happens.
  Both bit: every shadow smoothing size read a 0.00 difference through
  `saveImage` because the shadow map is an `SoSceneTexture2` with its
  own framebuffer, and the entire class of "Coin stomps a buffer the
  backend owns" is invisible to it. Use
  `v.getViewer().grabFramebuffer()` for anything on-screen-shaped, and
  `saveRenderDump` for the backend's own buffers.

This closes the "no reliable way to verify rendering" gap: the SwiftShader
blindspot is covered by the desktop leg being a *real-GPU readback* of the
same knob-for-knob staged frame.

The user-shader feature (section 6) has its own companion harness,
`scripts/user-shader-verify.sh`: a desktop leg running the
document-object-model GUI suites under xvfb (`user_shader_params.py`,
`user_shader_post.py` — property binding, per-binding overrides,
activation/deactivation with byte-exact restores) and a viewer leg
re-running the pipeline against a live headless-Chromium WASM viewer
(`user_shader_viewer.py` scene-graph route,
`user_shader_viewer_appearance.py` document-object route).

### 5.1 What this harness cannot see

Recorded because each entry cost a session, and because a harness that
passes on a build the user can see is broken is worse than no harness.

- WARNING: **menu behaviour, entirely.** Three ways of driving a menu
  were measured against a visibly broken build and all three passed:
  (a) `QTest.mouseClick(widget)` posts straight to the widget and never
  takes the popup grab (`menu_dismiss_probe.py`, 12/12 green while the
  menu bar was swallowing clicks); (b) **xdotool/XTEST** moves the real
  pointer onto the right widget with the popup up and the application
  receives *nothing*, `underMouse()` staying false under xvfb and under
  real XWayland alike; (c) posting to the `QWindow` through
  `QWindowSystemInterface` (`menu_opener_probe.py`) passes 5/5 on the
  broken build too. The likely common cause is that no real grab is ever
  taken without a window manager. **The user is the only oracle for
  menu behaviour**: report it as unverified, never as a pass.
- WARNING: **probe key presses must go through
  `QTest.keyClick(mw.windowHandle(), ...)`.** A key event sent to a
  `QWidget` never reaches Qt's shortcut map, so accelerator tests pass
  against dead accelerators.
- WARNING: **PySide deletes menu widgets you inspect.** Reaching an
  entry through `QWidgetAction::defaultWidget()`, or holding any QWidget
  wrapper across statements, makes PySide take ownership and collect it,
  killing the C++ children. It surfaces as "Internal C++ object already
  deleted" on something that was alive a line earlier, and reads as the
  menu rebuilding itself. Use `findChildren` plus a parent-chain
  ancestry filter and reduce everything to `str`/`int` inside the loop.
  Probes only; in C++ the menu owns them.
- WARNING: **`updateGui()` never delivers a deferred delete**, so a probe
  that holds the call stack is blind to every cleanup a destructor does.
  `QApplication::processEvents()` skips `DeferredDelete` events, and a
  *nested* `QEventLoop` skips them too: the event is posted at the outer
  loop level and waits for that stack to unwind. A probe that wants what
  a user gets from clicking has to run each step from its own main-loop
  callback, i.e. a `QTimer::singleShot` chain. The cost of learning
  this: "leaving Transform edit leaves the object drawn transparent"
  survived two sessions as a rendering bug and was the probe's own call
  stack keeping the task dialog -- and with it the object's on-top
  registration -- alive. Compare `dragger_stale_probe.py` (one long
  function, reads the defect) with `dragger_stale3_probe.py` (callback
  chain, same build, reads 0.0000 on all three render paths).
  `QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)`
  forces delivery regardless of loop level and is the quick way to tell
  a deferred destructor from a stale frame.
- WARNING: **startup `Console().Log` output is invisible to a probe.** A
  probe script runs long after the splash, and enabling logging from
  Python is too late for anything that happened during it. Pass
  **`--log-file <path>`** and read the line back from the file;
  `scripts/renderer-desktop.sh` takes **`FC_ARGS`** for exactly this.
- WARNING: **a `gdb --batch` run leaves FreeCAD alive** after the script
  ends, so kill it by PID. `pkill -f '<pattern>'` matched *its own
  shell* and killed the cleanup instead of the application, twice,
  leaving stray GUI windows behind.
- NOTE: **for a Coin console warning, break on
  `SoDebugError::postWarning` under gdb and read the backtrace.**
  Matching a message to a plausible call site got two of three warnings
  wrong: neither the place nor the cause was what the text suggested,
  and both were real bugs that had been called cosmetic
  (`docs/CoinRetirement.md` stage 1a).
- WARNING: **a probe that sets a `RenderDebug_*` parameter writes the
  USER'S config, and every later run inherits it.** These are global
  `RenderParams` (`User parameter:BaseApp/Preferences/View/Render`), not
  view properties -- setting one on a view is inert (2.3), so a probe has
  to set the parameter, and a probe that exits without clearing it leaves
  it set for the desktop session too. What makes this worse than a wrong
  picture is the shape of the failure. A probe that left
  `DebugViewMode` at 11 turned every subsequent capture into the id
  image, which is opaque and centre-sampled; the next question asked of
  those captures -- "does this render the same twice?" -- came back a
  clean 33/33, because an id image has no partial coverage and so cannot
  show a coverage defect at all. **A false PASS, from a stale
  preference.** Run such a probe under an isolated `XDG_CONFIG_HOME`, or
  clear the key afterwards, and check `preferences` in a sidecar before
  trusting a capture set.
- IMPORTANT: **the sidecar's preferences are an ALLOWLIST, and a key that
  is not on it does not restage.** `viewKeys` in `View3DInventorPyImp.cpp`
  names what goes out; anything else is invisible to a golden.
  `AntiAliasing` was missing from it until 2026-09-01, which meant a
  golden captured multisampled compared against whatever the running
  profile happened to say -- diverging along every silhouette in every
  stage, for a reason no sidecar reported. The `msaa` field records the
  resolved sample count, but nothing ever restaged from it. When adding a
  preference that changes what a frame looks like, add it to `viewKeys`
  in the same commit.


### 5.2 The committed test set (what ctest actually runs)

Everything above describes the harness. This is the part of it that is
wired into the build, so a regression is caught by a test run rather than
by someone remembering to capture a set by hand -- `tests/render/`.

**Nothing else in ctest draws a pixel.** The suites whose names suggest
rendering -- `RenderProperties`, `RenderCacheMaterial`, `MaterialXGen`,
`MaskedOcclusion`, `CullBenefit` -- are all deliberately built without a
GL context, and `PublishOnly_tests_run` goes further and asserts that no
driver is even mapped. That is the gap this set closes.

| test | what it is | cost |
|---|---|---|
| `RenderSmokeVg_tests_run` | `fcvgsmoke`: bgfx up headless in its own process, vg paths/gradients/strokes/text drawn offscreen, pixels read back, ink checked per primitive | 0.3 s |
| `RenderSmokePage2D_tests_run` | the same binary's retained-`Page2D` scenario: pan, in-band zoom, band crossing, rotation, damage, removal | 0.3 s |
| `RenderGoldenRaster_tests_run` | `scripts/render-test-scene.py` staged in a real FreeCAD on the platform's own display leg (xvfb on Linux -- see 5.2b), one camera, the five pipeline stages, compared against blessed references | 28 s Linux, 21 s macOS |
| `RenderGoldenRasterFlat_tests_run` | the same scene and stages with `FC_RENDER_TEST_BG=0`: the environment still lights the model but is not drawn, so the frame is the model | 20 s |
| `RenderGoldenCycles_tests_run` | the same scene path traced on the CPU (64 spp, 240x180) | 29 s |
| `RenderGoldenChess_tests_run` | the MaterialX chess set: a real asset with a real material library, raster and path traced | 65 s, see below |
| `RenderGoldenChessFlat_tests_run` | the chess set the same way, background off | 65 s |

The first four run in a default `ctest`. The path-traced legs and the chess set are opt-in:

    cmake -DFC_RENDER_HEAVY_TESTS=ON <build> && ctest -L render-heavy

WARNING: **a ctest LABEL does not keep a test out of a default run.** It
only gives `-L` something to select on; `ctest` with no arguments still
executes it. Neither does the `CONFIGURATIONS` property, which is not
filtered at all when no `-C` is passed -- measured directly: a test
carrying `CONFIGURATIONS render-heavy` ran anyway on a plain `ctest`.
**Registration is the only gate that actually holds**, which is why the
heavy tests are behind a CMake option and not behind their label alone.
The label is kept so `-L` can select them once the option is on.

**The reference images are their own repository**
(`realthunder/fcad-render-refs`, branch `LinkVibe`), mounted at
`tests/render/refs` as a git submodule (`git submodule update --init
tests/render/refs`), because a golden set is binary and is rewritten on
every reblessing -- churn that does not belong in the history of the
source tree. Every golden test is *skipped, not failed*, when that
checkout is absent, the same courtesy `MaterialXGen_tests_run` extends to
the MaterialX submodule. Its README carries the reblessing procedure; a
reblessing is a commit there and a submodule bump here.

#### 5.2b The platform legs, and why a golden belongs to one backend

`scripts/render-verify.sh` picks its leg from `uname`, and it is the only
place that knows how a display is raised:

| | Linux | macOS |
|---|---|---|
| display | `xvfb-run` + `QT_QPA_PLATFORM=xcb` | the real window server, `QT_QPA_PLATFORM=cocoa` (a window appears) |
| backend | `bgfx - OpenGL` (llvmpipe headless, `--gpu` for the device) | `bgfx - Metal`, registered by `FC_BGFX_METAL=1` |
| launch | `setsid nohup`, cleanup kills the process group (xvfb-run, Xvfb and FreeCAD are three processes) | `nohup`, cleanup kills the pid -- there is no group, and no `setsid` to make one |
| build tree | `build/conda-relwithdebinfo-801` | `build/mac-relwithdebinfo-801` |

Isolation is unchanged by that: what keeps a capture off the live session
is the private `XDG_*` dirs and `--user-cfg`, not the display. The macOS
leg does need a logged-in window server, so it cannot run over a bare ssh
session. `--gpu` is a no-op there -- every macOS run is already on the
device.

WARNING: **On macOS the golden tests need somebody logged in, and the
failure says nothing about it.** The registration gate runs
`find_program(xvfb-run)` on Linux and lets **Darwin through
unconditionally** -- there is no xvfb to find on macOS, because the
capture draws on the window server of the logged-in session. So the
gate asks whether a display *could* be raised and takes yes for an
answer on Darwin. Run `ctest` on a Mac sitting at the login window and
both golden legs register and then fail with

```
Cannot create window: no screens available
Abort trap: 6
CAPTURE FAILED (no DONE within 420s)
```

which looks exactly like real golden divergence. The three-way
confirmation, since the error text is useless on its own: `who` prints
nothing, `/dev/console` is owned by `root` rather than a user, and
`launchctl managername` says **Background** rather than **Aqua**. Log in
at the console and re-run; an ssh or background session cannot do it at
all. This is the first thing a macOS CI runner would hit.

`tests/render/CMakeLists.txt` requires `xvfb-run` **on Linux only**.
Requiring it everywhere is why the golden tests on macOS did not merely
skip: they were never registered, so a `ctest` run there was short two
tests and said nothing about it.

*** **What the Linux gate actually witnesses: llvmpipe, on both sides of
the comparison.** `fc_add_golden_test` calls `render-verify.sh` with no
`--gpu`, and it cannot: that leg needs a real Wayland socket and opens a
window on the desktop, which is not something a `ctest` run can do. So
the registered Linux legs -- the ones that blessed the references and
the ones that check them -- are the default headless `xvfb` leg, and
that is `bgfx - OpenGL` on **llvmpipe**, a software rasterizer. Measured
on the Linux box 2026-09-08 through the device field `640ed8a4d5` added:
`OpenGL 2.1 / llvmpipe (LLVM 20.1.2, 256 bits) / 4.5 (Compatibility
Profile) / Mesa 25.2.8 / vendor 0x0000 device 0x0000`.

That is not a defect, and it is the right default -- a change that is a
no-op by construction is exactly what a software rasterizer can prove,
deterministically and without a GPU in the room. But read the two tiers
for what they are: **Linux gates the logic, macOS gates the device.**
Every macOS run is on the real window server and the real Metal device
(`--gpu` is a no-op there), so the Metal sets are the only blessed
references on this project taken on hardware. A green Linux golden run
is not evidence about driver-shaped behaviour, and the open
`raster/mode0` residual below is exactly the kind of thing it cannot
settle.

*** **The four OpenGL reference sets record no device at all.** They
predate `640ed8a4d5`, so `refs/raster`, `refs/raster-flat`, `refs/chess`
and `refs/chess-flat` have no `device` field in their sidecars and what
hardware blessed them is written down nowhere. Only `refs/*-metal`
carries it. Re-blessing is the only way that gets fixed, so if a set is
re-blessed for any other reason, that is the moment.

**`refs/raster-metal` and `refs/raster-flat-metal` are blessed** (2026-09-07,
macOS 12 / Metal, Intel iGPU `0x8086 0x1622`), so
`RenderGoldenRaster_tests_run` and `RenderGoldenRasterFlat_tests_run`
now register and pass on the macOS leg -- 21.4 s each. Both were
restaged from their OpenGL siblings, so the camera is byte-identical to
the one those were blessed on, and two such captures compare byte-exact
at `--tol 0`.

**`refs/chess-metal` and `refs/chess-flat-metal` followed the same day**
(section 5.2c), so the real-document leg gates on Metal too. Only the
Cycles set has no Metal counterpart, and cannot get one from this box:
`BUILD_CYCLES` is off and the `src/3rdParty/cycles` submodule is not
checked out.

**A golden is a picture of one backend, and the blessed sets are
OpenGL.** So `fc_add_golden_test` looks for `refs/<set>-metal` on Apple
and registers nothing until that set exists. Handing the OpenGL set to
Metal is not a stricter test, it is a different one, and the first run
that tried it said so (2026-09-07, this box, Metal on `0x8086 0x1622`):
with the camera restaged from `refs/raster` -- byte-identical Coin camera
string, identical 858x608 viewport -- every stage diverged (depth 24.8%
of pixels past tolerance, normal 19.0%, AO 25.3%, shadow 18.7%, beauty
89.9%), and the defects behind that are worth stating exactly, because
they are the first thing a Metal blessing would have to fix.

**All of them are fixed as of 2026-09-07**, and the same restaged run
now compares against the OpenGL golden at depth 0.0002% of pixels past
tolerance (one pixel), normal 0.0002%, AO 0.1248% (mean 0.02), shadow
0.0002%, beauty 0.3527% (mean 0.17), with `geometryPixels` 108191 --
the golden's own count. Two defects were diagnosed here first and a
third only surfaced once they were out of the way; each is recorded
below with what it actually was, because the diagnosis is the expensive
half and the next backend will meet the same three.

*The frame is vertically mirrored.* Flip the golden and **100.00%** of
the Metal geometry mask falls inside it, at zero shift; the depth stage
then agrees to max delta 29, mean 2.01 (14.8% of pixels past tol 3),
where the best-fitting shift left 92% of them past it. So this is an
origin-convention fault, not a camera one. The capture path does ask --
`BGFXFrame.cpp` 6494, `const bool flip = caps->originBottomLeft`, which
reverses the rows on GL and correctly does not on Metal, where the flag
is false -- so the fault is upstream of the readback.

**The cause is `vs_fc_comp.sc`, confirmed by experiment.** It derives its
UV straight from clip space, `v_texcoord0 = a_position.xy * 0.5 + 0.5`,
under a comment reasoning that "the OIT targets are rendered by the same
backend, so no cross-API Y-flip is needed". The premise is true and the
conclusion does not follow: bgfx's V origin for a render target differs
by API whoever wrote it, so `y*0.5+0.5 -> 1` samples the top on GL and
the bottom on Metal. Adding `#if !BGFX_SHADER_LANGUAGE_GLSL
v_texcoord0.y = 1.0 - v_texcoord0.y; #endif` and rebuilding **only** the
shader assets (`ninja Renderer_assets`, no C++ rebuild) inverted the
mirror exactly -- containment of the Metal geometry against the golden
went from 62.05% upright / 100.00% flipped to 100.00% upright / 62.05%
flipped -- while `geometryPixels` stayed at 75236, not one pixel moved.
So the two defects are independent: this UV is the whole of the mirror
and none of the clipping. What the stage diff does with the mirror
corrected sets the expectation for a real fix: depth falls from 24.8% of
pixels past tolerance to 8.3%, normal 19.0% to 7.1%, shadow 18.7% to
8.7%, AO 25.3% to 20.0%, beauty 89.9% to 78.8%. Roughly a third of each,
and what remains is the clipped half -- so fixing the origin alone still
leaves every stage failing, and neither defect can be signed off on its
own.

That one-liner was the cause, not the whole fix, because the file is
misnamed by its own comment: `ensureProgram` pairs `vs_fc_comp` with
`fs_fc_present`, `fs_fc_depthenc`, `fs_fc_debug`, `fs_fc_comp`,
`fs_fc_env`, `fs_fc_sun`, the bloom chain, `fs_fc_ssao`/`fs_fc_gtao` and
their blurs, `fs_fc_cavity`, `fs_fc_shadow_blur`, the volume passes,
`fs_fc_cycles_blit` and the user volumetrics -- it is the engine's
fullscreen vertex shader, which is why the DEBUG stages were mirrored
too (`m_progDebug` is `vs_fc_comp` + `fs_fc_debug`).

**What shipped is `fc_screen.sh`**, which owns both directions of the
conversion -- `fc_clipToUv` for the UV of the pixel at a clip-space xy
(the fullscreen vertex stage, and the water SSR's own perspective
divide) and `fc_uvToNdc` for the inverse (`fc_prepassViewPos`, `volRay`,
`debugRay`, the env/sun/ground-shadow rays, and the impact splat that
must land on one named texel). Both are identities on GL, so the OpenGL
frame is unchanged by construction. See `docs/RenderEngine.md` sec 3.6.

**One thing feared before the fix turned out not to be true**, and it is
worth correcting here because it was the argument for delaying: flipping
`vs_fc_comp` does NOT leave two conventions live beside the passes that
build their own UV from `gl_FragCoord.xy * u_viewTexel.xy`
(`fs_fc_groundrefl`, `fc_glass_fs.sh`, `fc_line_sdf_fs.sh`).
`gl_FragCoord.y` counts from the same edge the texture v does on GL,
Metal and Vulkan alike, so those passes were always right on both, and
the flip brings the clip-space route INTO agreement with them rather
than out of it. What did have to be swept was every open-coded
`*0.5+0.5` and `*2-1` on a screen UV -- `fs_fc_ssao`'s sample
projection and `fs_fc_debug`'s `debugRay` were found only after the
first two fixes landed, by the AO and shadow stages still diverging.

Worth knowing, from before the fix: across `*.sc` and `*.sh`,
`BGFX_SHADER_LANGUAGE_GLSL` matched exactly one file,
`fs_fc_groundshadow_plane.sc`, and what it handles there is the
[-1,1] -> [0,1] `gl_FragDepth` conversion. So the tree had one precedent
for the projection fix and none at all for the origin one.

*And the far half of the scene is clipped away.* With the flip
understood, what survives is the FAR half -- which is exactly what
feeding a GL [-1,1] projection to a [0,1] clipper predicts, and the
reason to read the two defects together rather than separately. The
floor ends in a flat cut instead of its far apex, and the glass rod and
the bulb are gone entirely (`geometryPixels` 75236 against 108191). The
camera projection is the one matrix that reaches bgfx unconverted: Coin
builds it GL-convention (`View3DInventorViewer.cpp`,
`cam->getViewVolume(aspect)`) and `BGFXFrame.cpp` hands it to
`bgfx::setViewTransform` as it stands -- 3292 for the scene views, and
3520, 3535, 3680, 3803 for the rest -- while `view->projMatrix` carries
the same bytes on to the shaders and the CPU-side culling. Everything
else consults `caps->homogeneousDepth` (1610, 1617, 1627-1628, 1713,
`ProxyHierarchy.cpp`, `OcclusionCull.h`), but those are secondary
consumers; the primary camera transform is the one place that does not,
which is what you would expect of code no non-GL backend had run. For
this scene's camera the constants are unambiguous: Coin's GL ortho gives
`P[10] = -0.023595`, `P[14] = -1.038783`, while a correct [0,1] ortho for
the same near/far is `-0.011797` and `-0.019392` -- a factor of two on
one and nothing like a scale on the other, so a dump of what the frame
receives settles it in one line.

**What shipped**: `render()` remaps the fed matrix once, at the top of
the frame, `z -> (z + w) / 2` on the z row alone, and everything
downstream sees a matrix matching `caps->homogeneousDepth`. The w row is
left alone on purpose, so the perspective test every shader makes still
reads -1 or 0, and no shader reads the z row at all -- which is why the
conversion turned out to be local to clipping after all, in spite of
`setViewTransform` also feeding the predefined `u_proj`. Two consumers
needed a say: the scene publish and dump keep the matrix as Coin gave
it, because their viewer renders on a backend of its own, and the
Gribb-Hartmann frustum extraction now branches on the convention for its
near plane (`w + z` under GL, plain `z` otherwise). On GL the remap does
not run, so the OpenGL frame is unchanged by construction.

*And a third defect was hiding behind those two: an orthographic camera
read as a perspective one.* With the mirror and the clipping fixed, the
beauty stage was still 72.6% divergent while depth had fallen to
0.0002%. Geometry was right and shading was not, and the picture said
where: the environment background was a white starburst radiating from a
vanishing point, and the whole frame ran 33 levels bright. The cause is
that GLSL indexes a matrix by COLUMN and every other language bgfx
targets indexes it by ROW (`BGFX_SHADER_MATRIX_COLUMN_MAJOR`,
`bgfx_shader.sh` 25). bgfx makes `mul()` agree across that split, and
`mtxFromRows`/`mtxFromCols` build a matrix either way, but a written-out
`m[i][j]` names transposed elements -- silently, with no compile error.
A dozen shaders tell a perspective camera from an orthographic one by
`u_proj[2][3]`, the w row's z entry, -1 or 0; off GL that subscript
lands on the z row's w entry instead, which is never 0. So every
orthographic camera was taken for a perspective one, and the ray fan
rebuilt from `u_proj[0][0]` -- 0.0227 for this camera, read as a
perspective x scale -- fanned 44x too wide across the sky.

**What shipped**: `fc_matrix.sh` and `FC_MTX(m, i, j)`, which names the
element the CPU wrote at `float[16]` index `4*i + j`; the 78 subscripts
across 14 files were substituted mechanically, index for index, so the
GL expansion is character-for-character what was there before. Nothing
else in the shader set indexes a matrix: every construction already went
through `mtxFromCols`, and `mul()` was never at risk.

WARNING: **Grep for the convention, not for the symptom.** This one cost
a full diagnosis round because the first two defects masked it, and it
is the kind that will recur: `#if BGFX_SHADER_LANGUAGE_GLSL` and
`BGFX_SHADER_MATRIX_COLUMN_MAJOR` in `bgfx_shader.sh` are the complete
list of what bgfx says differs per backend and does NOT paper over.
Before blaming a new backend's driver, read that file and check the
shader set against it.

WARNING: **Test the flip before the shift.** Two wrong readings were
published here before the right one, and both are cheap to repeat.
First, "compressed 1.65x vertically" -- a bounding-box artefact: the
ratio was the clipping, not a scale. Then "translated 119 px, with the
surviving depth inverted" -- a silhouette coincidence, because the near
and far halves of this floor are similar triangles and a shift matched
one to the other at 99.90% while the flip that matches at 100.00% went
untested. The "inverted depth" was the same flip seen through mode 1,
which is `1.0 - clamp(prepass linear view depth * u_debugParams.y)`
(`fs_fc_debug.sc`): a NORMALIZED PREPASS quantity written as `-v_vpos.z`
from the model-view transform, not the depth buffer and not a function
of the projection at all. It was never evidence about clip space. A
mask comparison that does not try `flipud` first can align two lobes of
a symmetric silhouette and read as confirmation.

WARNING: **Identical divergent-pixel counts across probes of unrelated
quantities mean the probe is not reaching the quantity -- you are
measuring the frame, not the value.** From the Vulkan NaN hunt
(2026-09-08): three probes aimed at three different quantities returned
4827/16513/4456, 4818/16493/4457 and 4826/16512/4456. Counts that close
across unrelated subjects are not corroboration, they are the
instrument describing itself, and four "eliminations" built on them
were void -- two could not have produced a positive result even against
a guilty subject, because the defect was a NaN and `0 * NaN = NaN`
survives every gate that multiplies a suspect term out. Same species as
the flip-before-shift warning above: a measurement that confirms
whatever you point it at.

The discipline that follows: **self-test the instrument before trusting
a negative.** Light the classifier deliberately -- a `sqrt` of a
negative from a uniform lit 85k pixels and made the negatives
trustworthy -- and note that `(x-x)/(x-x)` does NOT work, the compiler
folds it. A classifier that has not been shown to fire is not evidence.
The same rule applies to a unit test: `ClipConvention_tests_run` was
checked by injecting the regression it exists to catch (folding the w
row alongside the z row), which failed 4 of its 7 cases -- and notably
NOT the case that asserts the measured pair, which passed throughout.
A test suite nobody has watched fail is a suite of unknown strength.

**The containment check is `scripts/render_contain.py`** (added 2026-09-08;
the diagnosis above was done with an ad-hoc script that was never committed).
It builds the geometry mask from the depth stage -- mode 1 is the normalized
prepass depth on black, so "not background" is the mask -- and reports
containment, `|current AND golden| / |current|`, under identity, `flipud`
and, with `--shift N`, the best integer offset. The asymmetry is the point:
a frame missing its far half is 100% contained and a mirrored one is not,
so `--both` separates a clipped frame from a moved one. The mask is close
to but not the sidecar's `geometryPixels` -- 106114 against 108191 on
`refs/raster`, the difference being geometry that quantizes to black in the
PNG -- so read it for placement, not as a pixel budget.

```sh
# the two blessed sets agree: identity 100.00%, flip 43.66%
.conda/run.sh python scripts/render_contain.py \
    tests/render/refs/raster tests/render/refs/raster-metal --both
# what the mirror looked like: identity 43.66%, flip 100.00%
```

It prints the transforms in the order the warning above insists on, and
exits non-zero when anything but identity fits better.

A fourth reading worth naming as wrong: the sidecar's `avgColor` is not
the PNG's mean, and it does not even track it in sign. It is measured on
the engine's own buffer, and it sits about 10 levels under the written
frame's mean at rest (golden: 145.15 recorded, 155.48 in the file). On
the run where the background ran 33 levels bright it read 143.4 -- LOWER
than the golden's 145.15 -- while the PNG's mean was 188.93 against
155.48. It would have said the two frames nearly agreed. Diff the
pixels; `avgColor` is a capture fingerprint, not a measurement.

WARNING: **A poll pattern that works under GNU grep can hang the whole
leg elsewhere.** `render-verify.sh` waited for the capture with
`grep -q "^DONE$\|^ABORT"`, and a `$` in MID-pattern is an anchor only
in ERE: POSIX BRE reads it as a literal dollar, and only GNU grep bends
that rule before a `\|`. So on macOS, where `/usr/bin/grep` is BSD grep,
the poll asked for a line beginning "DONE$", never matched, and every
capture sat out its whole `--timeout` after the work was finished --
426.5 s of a 420 s budget under ctest for a capture that takes about 20.
It looked exactly like a hang, and was mistaken for one. Both polls
(here and in `user-shader-verify.sh`) now use `grep -qE`. The two golden
tests run in 21.4 s each on the macOS leg, not 426.6.

The same idiom without a mid-pattern `$` is fine, and is left alone in
`gui-test.sh` and `file-blob-verify.sh`: BSD grep does support `\|`, and
`^FAIL\|^ABORT` matches on both. It is only the anchor that differs.

WARNING: **A restaging must not carry the backend across.** The sidecar records
the whole `View/Render` group, `Type` included, and that key describes
the machine that blessed the golden rather than the picture it blessed.
`render_verify.py` skips it (`RESTAGE_SKIP`): replaying it put a macOS
run on `bgfx - OpenGL`, where Apple's 2.1 compatibility profile cannot
run these shaders at all -- the renderer stands aside for the render
cache, and all five stages then failed with `Frame capture timed out`
waiting for a frame that was never coming. The platform picks its own
backend in `render-test-scene.py` (`FC_RENDER_BACKEND` overrides), and
the sidecar's own `backend` and `device` fields are where the blessing's
identity is recorded.

**The two scenes are deliberately different in kind.**
`scripts/render-test-scene.py` is four primitives built in process -- a
matte floor, a rough box, a metal sphere, a glass rod and one
shadow-casting bulb -- chosen so that one cheap scene still puts content
in every stage the diff walks, the shadow buffer included.
`scripts/render-test-chess.py` imports the MaterialX chess set from the
submodule, which is the case that exercises map binding, the texture path
and the MaterialX splice. A synthetic scene cannot fail the way a real
document does, and a real document is too slow to run every time; hence
one of each.

**Each scene is registered twice, with the background drawn and without
it** (`FC_RENDER_TEST_BG`, read by both scene scripts). They are not the
same test. With a background most of the frame is scenery, so a change to
the model moves a few hundred pixels while a camera that lands slightly
differently moves a hundred thousand and buries it. Without one, the
frame is the model and the diff is about what is under test. The
background is drawn by the engine too, so neither case replaces the
other.

WARNING: **the flat leg has to turn off the environment, not just the
gradient.** `Render_PBREnvBackground` defaults to *on*, and the drawn
environment sits over the viewer's gradient -- so a scene that clears
`Gradient`/`RadialGradient` and sets `BackgroundColor` while leaving the
environment alone produces the *same frame* as its lit sibling. That was
`refs/raster-flat` as first blessed on 2026-09-05: its beauty frame
differed from `refs/raster` on 7.9% of pixels with a maximum channel
delta of **1** -- twenty seconds of default `ctest` spent re-testing the
frame the previous test had just checked. `render-test-chess.py` had it
right (`p.SetBool("PBREnvBackground", BACKGROUND)`); `render-test-scene.py`
did not, and now does. Blessed correctly, the two legs diverge on 79.5%
of the beauty pixels (max 121) with depth, normal, AO and shadow
byte-identical -- the same geometry, a different background, which is
exactly the shape the pair should have. If a *-flat set ever compares
near-identical to its sibling, this is the first thing to check.

*** **CLOSED 2026-09-08: the `raster` beauty stage did not compare
byte-exact, and the cause was a stale reference.** A fresh Linux capture
restaged from `refs/raster` matched depth, normal, AO and shadow
byte-for-byte and diverged on the beauty stage alone: **7.9421% of
pixels past `--tol 0`, max channel delta 1, mean 0.08**. Every one of
those pixels was on the environment background and none on geometry;
`refs/raster` needs re-blessing and nothing needs fixing. The diagnosis
is kept in full because it took two sessions and three hypotheses, two
of which were wrong, and the wrong ones are the reusable part.

Three properties of the number mattered, in the order they were found:

- **It is bit-stable.** The same run repeated on a quiet box gives
  7.9421% both times, to four decimals. So it is a deterministic
  difference between what the tree renders now and what was blessed,
  not run-to-run noise, and it will not wash out by re-running.
- **The llvmpipe leg itself is bit-deterministic, so this is not
  render noise.** Tested 2026-09-08: a capture saved, the golden test
  re-run, and capture diffed against capture at `--tol 0 --frac 0` --
  all five stages OK, and the two beauty PNGs share an md5
  (`09ac9269585855cb0986d0848aba941e`). Two renders that ought to be
  identical *are* identical, to the byte. An earlier guess that ~7.9%
  at max 1 was the beauty stage's characteristic 1-LSB population under
  software rasterization is therefore **wrong**, and is recorded here
  only so nobody re-derives it.

So the difference is between **what the tree renders now** and **what
was blessed**, and it is carrying information rather than noise.

**The chronology narrows it to a four-hour window, and `refs/raster` is
the only set inside it.** Blessing times against the commits of the same
day:

| | |
|---|---|
| `6e8b01e` 09-05 **08:40** | `refs/raster` blessed |
| `bbb144e104` 09-05 11:48 | a frame dump waits for a complete frame |
| `01470cc` 09-05 **11:51** | `refs/chess`, `refs/chess-flat` blessed |
| `638d3ab1c7` 09-05 13:06 | every capture waits on the complete-frame signal |
| `0a79dec` 09-05 **16:15** | `refs/raster-flat` re-blessed |

Every set blessed after 11:48 compares byte-exact today; the one set
blessed before it does not. That is a correlation and not yet a cause --
and note the mechanism does not obviously fit, since an incomplete frame
means a stand-in shader or a missing shape, which would diverge by far
more than one level.

**RESOLVED 2026-09-08: it is the environment background, and
`refs/raster` is a stale blessing rather than a defect.** The test was
to intersect the divergent-pixel mask with the mode 1 geometry mask,
and the answer was not close:

```
frame            858 x 608 = 521664
geometry px      106114
divergent px     41431
  on geometry    0
  on background  41431      9.97% of the background area
max channel delta 1
```

**Zero divergent pixels on geometry.** Not few -- none. What was
predicted from the areas alone was 10.02% of the background (geometry
108191 of 521664, so background 79.26% of the frame, and 7.9421% of the
frame is 10.02% of that); what was measured was 9.97%, the 0.05 point
being the quantize-to-black gap between the sidecar's `geometryPixels`
and the mode 1 mask. About one background pixel in ten sits across a
quantization boundary and rounds the other way than it did when the set
was blessed.

The reasoning that got there, since it generalizes: `refs/raster` draws
the environment background and `refs/raster-flat` does not, they are the
same scene on the same box, and it is the flat set that is byte-exact.
The environment was very nearly the only difference between the set that
diverged and the set that did not.

**Two consequences.** First, both halves of the cross-API work are
cleared by this: a change to clip depth, UV origin or matrix indexing
cannot produce a difference that is zero on every geometric pixel. That
was an inference from `max 1`; it is now a measurement. Second, the
"do not re-bless" instruction above is **withdrawn for this set**. It
was right while the difference might have carried information about a
defect. It does not -- the only thing it was protecting was the age of
the artefact -- so the action is to re-bless `refs/raster`, on a box
that records its device. It is an OpenGL set, so that box is the Linux
one; macOS cannot produce a GL capture here at all (Apple caps the
compatibility profile at 2.1).

And the point that costs nothing to state: **the `device` field would
have answered this in one step instead of two sessions.** The four
OpenGL sets predate it. Whatever else a re-blessing is worth, it is
worth that.

Worth knowing before chasing it: the Linux leg is a software rasterizer
on both sides (see 5.2b), so this residual has never been seen on real
hardware, and a driver explanation cannot be either confirmed or
dismissed from that box.

**The Cycles leg is reproducible as it stands, and must be kept that
way.** The offline path sets no seed, so the integrator default applies,
and denoising is enabled only on the *viewport* path
(`CyclesViewport.cpp`), never on `cyclesRender`. Measured on the small
scene: two restaged runs are byte-exact at `--tol 0 --frac 0`, and the
trace costs 1.3 s at 64 spp. CPU is the device on purpose -- it is the
one every box has, and two devices do not produce identical pixels, so a
set blessed on CUDA cannot be compared against a CPU run.

Captures are named so that `render_diff.py` needed no change to gain a
second renderer: a traced frame is written as
`<prefix>--cycles--mode0.png`, which its existing filename grammar reads
as a group of its own whose single stage is the beauty frame.

WARNING: **Bless the restaged set, never the fresh one** (the rule stated
in section 5, now with numbers). Fresh-vs-restaged on the small scene
differs on up to 0.0098% of pixels with a maximum channel delta of 254,
purely from the sidecar camera's ~8 significant digits;
restaged-vs-restaged is byte-exact.

**Path tracing amplifies that rounding, and the chess set shows it.**
Where the small scene's traced frame moved by a maximum delta of 1
between a fresh and a restaged camera, the chess set's moved by 53 over
0.25% of its pixels -- enough to fail the default tolerance. Nothing is
wrong with the renderer: a camera that differs in its last digit sends
different rays, and a detailed textured scene under an HDR environment
turns that into visibly different noise where four primitives under a
single bulb did not. It is the sharpest argument for the rule -- the
raster leg of the same run compared clean, so a set blessed from a fresh
capture would have looked fine right up until the traced leg was added.

`scripts/render-verify.sh` takes `--cycles`, `--cycles-device`,
`--cycles-samples` and `--cycles-size` for the traced leg.

#### 5.2a The chess set: the race the test found on its first day

`refs/chess` was not blessed with the rest of the set: on the raster leg
a piece of the MaterialX chess set intermittently came back plain
untextured white instead of jade-with-gold-trim -- 2 runs in 12, same
build, same restaged camera, nothing logged, the differing pixels in one
tight box around the black queen (y 318-400, x 325-361 at 858x582), the
Cycles leg of the same runs right every time, and a 4x settle no help.
Chased 2026-09-05 and fixed the same day. Three findings, all three now
in the code:

- **The white piece was a capture of a program still compiling.** A
  MaterialX material is compiled by a `shaderc` subprocess
  (`BGFXRendererLibP::ensureUserShaderBin`, asynchronous, cached on disk
  under `$XDG_CACHE_HOME/FreeCAD/BGFXUserShaders`), and until its binary
  lands `getUserProgram` returns an invalid handle and the draw uses the
  stock program: no maps, no material, white. Compile order is
  deterministic, the black set's surfaces are asked last (see the next
  item), and the black queen's is the last of those -- which is why the
  rare failure was always that piece and byte-identical. Fix: the frame
  records that a draw stood in, and the tail of such a frame does not
  consume a pending dump (section 4.2). The wait is bounded by the
  compile's own watchdog; a failed compile is recorded and never asked
  again, so a capture of a scene with a broken material still returns.
- **The scene was still arriving.** A publish that spends its capture
  budget (`Render CaptureBudgetMS`) leaves first-time shapes out of the
  frame and catches them up in follow-up publishes, one or more shapes
  per pass. On this box the chess set's black pieces reach the backend
  some fifteen seconds after the white ones, because the frames in
  between are cold -- generating fifteen MaterialX variants and, once
  their binaries land, building fifteen programs and uploading their
  maps on llvmpipe is a second or more per program. A dump consumed in
  that window is a picture of half a chess set. Fix: the viewer tells
  the backend before each frame whether the last publish deferred
  anything (`Renderer::holdFrameDump`), and the dump is held the same
  way. And a held frame counts as progress for `pumpFrameDump`: its old
  5 s timeout, measured from the request, expired inside the one frame
  that built five programs -- which is also why every wait tried before
  this one "did not work".
- **The deterministic repro measured the camera, not the material.**
  `--settle 0` failed 100% of the time at 31.7022% of pixels, and that
  number never moved, fixed or not. It was the camera: the scene's
  `fitAll()` animates the position into place in ten per-frame steps
  (`viewBoundBox` -> `animatedViewAll`), and with frames of seconds
  during the cold compile the animation was still overwriting the
  restaged camera thirty seconds later -- the diff image shows the board
  twice, offset, on an unchanged environment. Both `render_verify.py`
  (in `freeze()`) and the chess scene now switch navigation animation
  off (`setAnimationEnabled(False)`), so a fit and a `view<Name>()` land
  in one step. The material race was real and is fixed; its repro was
  measuring something else.

Verified after the fix: zero-settle restage captures of the chess set
compare clean against a good capture, run after run, with a private
shader cache (a cold compile every run); the default-settle runs the
same; and the rest of the render set unchanged. `refs/chess` and
`refs/chess-flat` are blessed from restaged captures, so
`RenderGoldenChess_tests_run` and its flat variant run with
`-DFC_RENDER_HEAVY_TESTS=ON`. If a chess diff ever again shows a lone
piece, re-read this section before calling it noise: the hold is what
guarantees the material, and a lone divergence around one piece would
mean it has been bypassed.

#### 5.2c The chess set on Metal: three faults, none of them the backend

Chased 2026-09-07, the session after the Metal shader conventions were
fixed (5.2b). The raster chess leg is the one that can break
independently -- it is the only test that exercises MaterialX, map
binding and the texture path, and the per-face texture and matcap
shaders were not part of that sweep. It came up clean on Metal:
**0.3288% of the beauty pixels past tolerance, max channel delta 140,
mean 0.16**, against the 0.3527% the raster pair shows. Nothing in the
material path needed a Metal fix.

Getting there took three, and every one of them is a *portability* fault
in the harness rather than anything the renderer did. They are worth
stating because each failed silently or nearly so.

- **The scene script named its backend.** `render-test-chess.py` set
  `View/Render` `Type` to `"bgfx - OpenGL"` as a literal, where
  `render-test-scene.py` picks per platform. The commit that gated the
  harness by platform updated one scene script and missed the other.
  On macOS that asks for a backend Apple's GL 2.1 compatibility profile
  cannot run these shaders on, so no frame ever came: all five capture
  steps failed, and what they *reported* was
  `FreeCADGui.ActiveDocument` being None.
- **The scene imported pivy, which this box does not have.** The camera
  was staged as `getCameraNode().orientation.setValue(coin.SbRotation(
  ...))`, and there is no `pivy` in the macOS conda env, so `stage()`
  raised at its second import and the document was never created. That
  is the None above. The staging failure was invisible because `say()`
  wrote to `FreeCAD.Console`, which in a GUI run goes to the report
  view: `run.log` held nothing. It now writes to stderr as well, which
  is what `render-verify.sh` redirects, and the traceback was in the
  log on the next run. The camera is staged through
  `View3DInventor.setCameraOrientation((x, y, z, w))` -- same
  quaternion, no pivy.
- **A restage replayed an absolute asset path from the blessing box.**
  With the scene finally staging, the frame came back with no
  environment at all: flat grey where Venice should be, the model lit
  by the fallback. 99.9782% of pixels, mean 71.09. The cause is in the
  sidecar: `Render_PBREnvImage` (and the `View/Render` preference
  behind it) is recorded as the absolute path of the HDR *on the box
  that blessed the set*, `/home/thunder/works/sw/fcad/...`, and
  `render_verify.py` wrote it back verbatim. The setting takes any
  string; the renderer says `cannot embed environment image ... does
  not exist` into the report view and carries on with the built-in
  environment. A capture of a scene with its environment missing, and
  a passing-looking harness.

  `relocate()` in `render_verify.py` now maps such a value onto the
  current checkout -- keep a path that exists, else take the longest
  tail of it that exists under the repository, else write nothing and
  report the setting as failed. Writing nothing is the point: the scene
  script's own value is a better answer than a dead path, and a
  reported failure is better than either.

**The registration rule this leaves behind.** The chess entries asked
for the traced leg unconditionally and so were gated on `BUILD_CYCLES`,
which this box does not have -- a blessed `chess-metal` would still
have registered nothing. They now register with the traced flags only
when Cycles is built, and `fc_add_golden_test` refuses to register a
test whose golden *holds* a traced frame when this build cannot produce
one (`render_diff.py` reads the missing group as a divergence, and
rightly). So the OpenGL sets keep their traced leg, the Metal ones are
raster-only until someone blesses them with Cycles built, and the
raster leg of the chess scene gates everywhere either way.

---

## 6. Endgame: user-loadable shaders

The debug machinery above is deliberately the **first constrained slice** of a
user-facing feature: letting users attach their own shaders at predefined
pipeline stages, with parameters bound to document/view properties by the
same naming protocol (`<Group>_<Name>` → shader parameter).

### 6.1 Authoring model: reuse Coin's shader nodes

Coin already ships a complete, persistable, Python-visible shader data model
(`src/shaders/` in the coin fork):

- `SoShaderProgram` — container; `SoMFNode shaderObject` holding the stages.
- `SoVertexShader` / `SoFragmentShader` (subclasses of `SoShaderObject`) —
  each with `sourceType` (`GLSL_PROGRAM` inline / `FILENAME`),
  `sourceProgram` (the source text or a path), and `SoMFNode parameter`.
- `SoShaderParameter1f/2f/3f/4f/1i/…/Matrix` + array variants — **each named
  uniform is a node with a `name` string and a typed `value` field**, plus
  `SoShaderStateMatrixParameter` for auto-fed modelview/projection.

FreeCAD currently uses none of these nodes anywhere (verified: zero
references in the fork), so this is a greenfield integration with no
compatibility baggage. We adopt these nodes as the **authoring and
persistence model**: what a user builds (from Python or a future material
editor), what gets saved in the document, and what Coin's own GL path can
even render directly for a degraded preview.

### 6.2 Bridging: the SoFCRenderMaterial pattern, again

The render-cache bridge already has the exact pattern needed, proven by
`SoFCRenderMaterial` (a node carrying no Coin GL state, existing purely to be
captured for the external backend). A shader node reaches bgfx through the
same 4-step chain:

1. Register a post-callback for the node type in `SoFCRenderCacheManager`
   (mirroring the `SoFCRenderMaterial` registration).
2. Copy shader identity + parameter block into the cache `Material` struct in
   `SoFCRenderCache` (and extend material `operator<` so materials with
   different shaders don't collapse into one draw batch).
3. Carry it through `RendererBridge::translateMaterial` into
   `Render::Material`.
4. Consume it in the backend: program lookup + uniform binding.

The backend treats a user shader like it already treats `water`/`glass`
material flags — a selector plus a parameter block driving either a branch in
the program-selection tree (for `material`-stage shaders) or a dedicated
pass (for `post`-stage shaders, mirroring the water-pass pattern).

Implementation notes from the first slice (post stage):

- **Scene placement.** A scene-level (`post`) program must sit at the top
  level of the scene graph: the cache manager only re-traverses invalid
  caches, so a program nested inside a still-valid cached separator is
  pruned with its subtree and vanishes from the capture on the next
  rebuild.
- **Compile off the paint path.** The backend render runs inside the Qt
  widget repaint; spawning the compiler there (blocking `QProcess`)
  hitches the frame and re-enters Qt's repaint machinery. Compiles are
  asynchronous: `render()` only consults the bin/program caches, missing
  bins queue a compile onto the event loop, and a finished compile bumps
  a generation that re-dirties the scene for idle-skip clients.
- **pivy ABI.** Adding fields to Coin node classes (the `stage` field)
  changes their object size — a pivy built against older Coin headers
  then heap-overflows on `coin.SoShaderProgram()` (the allocation size is
  baked into the wrapper). Rebuild pivy whenever the coin fork changes a
  node's layout; the pivy-feedstock needs the same bump for distribution
  images.

Implementation notes from the second slice (`material` stage):

- **Per-object attachment = the SoFCRenderMaterial placement rules.** A
  `stage="material"` program is routed by the cache-manager callback into
  the *enclosing* render cache (`SoFCRenderCache::setUserShader`) instead
  of the scene-level list: it applies to the shapes captured after it in
  the same cache (insert it like `ViewProviderGeometryObject` inserts
  `SoFCRenderMaterial` — child 0 of the view provider root) and, like the
  water/glass flags, does **not** merge from a parent cache into child
  caches. The translated program rides the cache `Material` as a
  `shared_ptr<Render::UserShader>` whose pointer identity keys draw
  batching (`operator<`) — a node edit invalidates the cache and
  re-translates.
- **Fragment stage only replaces the beauty shading.** The user program
  substitutes for the mesh program in the scene beauty passes (opaque /
  sorted-transparent / ground reflection) with the stock `vs_fc_mesh`
  vertex stage when the program carries none (fragment contract: `$input
  v_normal, v_color0, v_vpos`). The depth prepass, shadow casters,
  picking, highlight/on-top and WBOIT draws keep the stock programs (the
  user contract is one color output), section-clip discard is not applied
  to user programs, and a draw with a user shader is excluded from the
  cross-object instancing path. While the async compile is pending or
  failed the standard program stands in -- never a black object. A
  pending frame dump is not consumed by a frame that stood in (section
  4.2): the capture waits for the material, the screen does not.
- **User vertex stage.** A program carrying a vertex source replaces the
  stock `vs_fc_mesh` pairing (vertex contract: `$input a_position,
  a_normal, a_color0` / `$output v_normal, v_color0, v_vpos`; the
  predefined `u_modelViewProj`/`u_modelView` chain and the engine's
  `u_params` are available — bgfx uniforms are global by name). This is
  the displacement/particle route: positions are a pure function of the
  vertex attributes, `Param_*` uniforms and the clock below. Two known
  consequences of the stock passes keeping their own programs: line and
  point draws stay at the undisplaced geometry, and Coin's auto near/far
  planes fit the undisplaced bounding box, so a large displacement can
  clip (an emitter-bounds story comes with the particle framework).
- **Render-state override.** `App::ShaderProgram` carries `Blend`
  (Default / Alpha / Additive) and `DepthWrite` properties for the
  material-stage beauty draw — the states particles and glow effects
  need. A non-default combination rides the existing parameter channel
  as a reserved `SoShaderParameter` named `fc_state` (no `u_` prefix —
  it is not a uniform contract), so it reaches Appearance per-binding
  clones and the snapshot transport with no new node fields; the
  backend consumes it at the user-draw submit by re-recording the draw
  state. The stock passes (prepass, shadow, pick) are untouched, and a
  blended override stays in its stock draw bucket — cross-object
  blend-vs-transparency ordering is the emitter framework's problem,
  not this override's.
- **Emitter seed geometry.** `App::Shader` `Demo="Emitter"` generates
  the particle seed mesh: `EmitterCount` quads whose 4 vertices
  coincide at a random anchor inside the `DemoSize` spread box
  (deterministic per `EmitterSeed`). Zero area means the stock
  pipeline shows nothing; a particle vertex shader expands them into
  billboards from the seed attributes — `a_normal.xy` = corner (±1),
  `a_normal.z` = the particle's 0..1 index, `a_color0` = the
  per-particle random seed, `a_position` = the anchor. Combined with
  `u_fcTime`, `Param_*` uniforms and the Blend/DepthWrite override,
  position-as-`f(seed, t)` gives stateless GPU particles on every tier
  with no engine-side simulation. The mesh is built from explicit
  element nodes (`SoCoordinate3`/`SoNormal`/`SoMaterial`) — the render
  cache does not capture `SoVertexProperty`-fed shapes.
- **Animation clock `u_fcTime`.** The engine records `uniform vec4
  u_fcTime` with every consuming user draw (material and post): `.x` =
  seconds on the shared effect clock (the one driving water/fire/
  caustics), `.y` = 1 while the clock advances, `.z/.w` reserved. A user
  shader whose source references `u_fcTime` is thereby *animated* — the
  reference itself is the opt-in, no declared flag — and keeps the
  viewer's redraw loop alive exactly like the stock timed effects
  (`animating()` → `scheduleRedraw`). `RenderDebug_FreezeFrame` pins the
  clock to 0 with `.y` = 0 and stops the self-scheduling, so frozen
  captures of time-animated shaders stay byte-deterministic (the golden
  harness contract).
- **Lighting helper library.** A user material program that only wants a
  custom base color should not lose the engine's lighting. The stock
  mesh shading is factored into `fc_mesh_lighting.sh` (uniforms,
  samplers and `fcShadeFragment()` — headlight or shadowed scene light,
  Render_Light bulbs incl. their shadow tiles, fire lights, screen-space
  AO, the PBR branch), consumed by the stock `fc_mesh_fs.sh` and by the
  user-facing `fc_user_lighting.sh`: `gl_FragColor =
  fcLightFragment(base, v_normal, v_vpos)` shades any albedo exactly
  like the standard renderer (`fcStockBase()` returns the draw's stock
  base color, so the identity form reproduces stock rendering). bgfx
  uniforms/samplers are global by name, so the values and textures the
  engine records with the draw feed the include's declarations
  unchanged; `gl_FragCoord` rides a macro because shaderc's spirv path
  resolves it only inside `main()`. The includes ship in
  `assets/shaders/src`, and the user-shader compile cache key carries a
  content hash of that include tree, so a shipped-helper change never
  reuses stale cached bins.

### 6.3 Compilation reality

The bgfx backend consumes **precompiled per-API binaries** (`.sc` → `shaderc`
→ `.bin` loaded from `assets/shaders/{glsl,essl,spirv,metal}/`); there is no
runtime GLSL compiler. Coin's nodes carry *source*. Reconciliation:

- The Coin node is the source of truth; the backend maintains a
  **compile cache keyed on source hash × backend API**, invoking `shaderc`
  on demand. Section 3's hot-reload is the prototype of exactly this.
- **Dialects are extensible, not singular.** We own the Coin fork, and
  `SoShaderObject::sourceType` was designed as a multi-dialect enum
  (ARB/Cg/GLSL) — so we extend it (e.g. `BGFX_SC`) rather than inventing a
  parallel description. But a future backend may demand yet another dialect,
  so the node model must carry **multiple tagged source variants** of one
  logical shader: `sourceType`/`sourceProgram` stay as-is for the
  single-source case, and the program/object node gains the ability to hold
  alternates keyed by dialect. The bridge hands all variants + tags to the
  backend; each backend picks the dialect it can consume (compiling or
  transpiling as it knows how). bgfx's `.sc` is the first added dialect;
  plain GLSL remains valid and lets Coin's own GL path preview
  fragment-only `post` shaders.
- Browser tier: `shaderc` is a host tool, so the scene server compiles and
  ships binaries to the WASM viewer over the existing snapshot/asset
  protocol — same shape as every other asset the viewer already receives.

### 6.4 Design constraints (fixed)

- **Property binding via the protocol.** A dynamic property in the
  `Param` group of an `App::ShaderProgram` is a shader parameter: its view
  provider materializes it as an `SoShaderParameter` node on the shared
  Coin shader object, named by the section-2.5 rule (group prefix
  dropped, `u_` prepended — `Param_Tint` feeds `uniform vec4 u_Tint`),
  valued by the same property→vec4-lane packing as the `RenderDebug_*`
  view properties (one shared bridge helper). Only `Param_*` binds —
  dynamic properties in other groups stay ordinary properties.
  Editing the property updates the uniform live — the
  parametric loop reaching into shading — and every consumer of the node
  (demo preview, Appearance bindings, the direct scene-graph route, the
  browser tier via the snapshot's shader table) sees it, because the
  parameters ride the captured `UserShader` and are recorded with the
  consuming draws. Like-named dynamic properties on a `App::ShaderBinding`
  override the program's values for that binding only; a parameter the
  program does not carry is added, so a binding can drive any uniform the
  shader source declares. The backend zeroes every dynamically bound
  uniform that is not in the consuming draw's parameter list — uniform
  values persist backend-side between frames, so a removed parameter
  would otherwise keep feeding its stale value. One dialect declares
  its parameters the other way round: a MATERIALX program's `Param_*`
  properties are materialized FROM the document, which declares its own
  interface, and are withdrawn with it
  (docs/CyclesIntegration.md sec 6.11). Everything downstream of the
  property is the same chain.
- **Predefined stages, not arbitrary hooks.** Users pick named attachment
  points: `post` (full-screen pass over composited color — simplest, first),
  `material` (replace surface shading for an object), later possibly
  effect overrides (water/ground). Each stage documents its available
  inputs (samplers/targets) and a fixed output contract.
- **The stage is declared on the node.** `SoShaderProgram` (Coin fork) gains
  an `SoSFName stage` field (default `"material"`). A name, not an enum:
  stage names are renderer-pipeline-specific and will grow, and a name field
  needs no Coin recompile per new stage. `SoSFName` rather than
  `SoSFString` because `SbName` interns strings in Coin's permanent name
  hash — identical strings share one address, so the per-traversal stage
  dispatch is a pointer compare against pre-interned stage names, and it is
  the idiomatic Coin type for identifiers. The bridge passes the tag
  through; the backend maps known names and warns-and-skips unknown ones.
  Coin's own GL path ignores the field — it has no such pipeline stages,
  which is the correct degradation.
- **Sandboxed failure.** A shader that fails to compile logs the error and
  falls back to the standard program — never a black screen, never a crash.
  Compile errors are surfaced through the report view (and the control
  channel on the browser tier).

### 6.5 Document object model (settled 2026-07-25)

The user-facing carrier of shaders is a family of three document objects.
All three keep their `DocumentObject` class in `src/App/` — the headless
server tier and `FreeCADCmd` must restore both library and working documents
— with all behavior, Coin node ownership and editors on the Gui side.
Rationale for a dedicated binder object (instead of a link property on
consumers): FreeCAD object hierarchy is not scene-graph hierarchy — objects
may have no scene graph of their own, and one object may modify another's
visuals. Instance context ("this occurrence of a linked body, not all
instances") needs a link path, which only a link object or property owned
by a `DocumentObject` can carry — cross-document shader libraries likewise
need XLink machinery owned by a document object.

- **App::ShaderProgram** — one stage-tagged program: multi-dialect source
  variants (§6.3), the stage name, and its parameter set (`Foo_Bar` dynamic
  properties per §6.4).
- **App::Shader** — groups a list of ShaderProgram objects into one logical
  effect/material. One effect legitimately needs several programs: a cutout
  or displacing material needs matching depth-prepass and shadow-caster
  programs, a toon look = `material` shading + `post` outline, multi-pass
  post chains (blur, bloom) are N programs in sequence. Inert on its own —
  opening a shader-library document applies nothing. **Preview:** a
  `PropertyEnumeration` demo shape (None / Box / Sphere / Cylinder / Cone)
  with basic sizing properties lets the Shader's own view provider display
  the effect on a demo shape — Coin's builtin primitive nodes (`SoCube`,
  `SoSphere`, `SoCylinder`, `SoCone`; sizing properties map onto the node
  fields), no mesh generation of our own (verify the render-cache capture
  handles `generatePrimitives`-tessellated shapes; else fall back to a tiny
  `SoIndexedFaceSet` tessellation). Complex-shape previews use a normal
  Appearance binding instead.
- **App::ShaderBinding** (was App::Appearance) -- the binder that activates shading, an
  `App::LinkGroup` whose children carry both the effect and its scope. The
  shader is the first child that resolves (through any chain of links) to
  an `App::Shader` — use an `App::Link` child to pull the effect from a
  shader-library document. Every other child is a target. Plain children
  are claimed into the group in the tree; the convention for binding
  without restructuring the document — and for carrying instance context
  at all — is an `App::Link` child (a link whose subname path points into
  an assembly). The children render like any link group's, so a target
  link child also shows an instance carrying the effect; a shader-only
  Appearance (no target children) applies the effect's scene-level
  (`post`) programs globally — one uniform activation mechanism for both
  stages. Like-named dynamic properties on the Appearance override the
  shader's parameter values for that binding only ("same toon shader,
  different tint for these instances"). A `Scope` enumeration
  selects between the two application modes below (default `Object`;
  `Element` is the reserved follow-up value).

**Scope=Object — direct attachment, the cheap mode.** Each target is
resolved to its final object and the binding's own `SoShaderProgram` node
(per-binding parameter overrides baked in) is inserted at child 0 of that
object's view-provider root. The capture callback folds it into the
object's own render cache (`SoFCRenderCache::setUserShader` →
`Material::usershader`), and `mergeMaterial` merges it **down through all
child caches, outer shader wins** — exactly the link material-override
semantics (`SoFCSelectionRoot::setColorOverride` → `overrideflags`).
Because view-provider snapshots share the root's children, every instance
everywhere picks it up, in every view, at zero per-frame cost; a group or
assembly target shades all its children. Position inside the owning cache
is irrelevant (override semantics, not Coin's after-the-node material
rule). Note the one asymmetry: a subname shortcut link (`Link → 
A1."A2.Box."`) renders only the tail object's snapshot with a baked
transform — it bypasses the intermediate objects' nodes, so a shader
attached to `A2` does not reach that shortcut's Box (it is an instance of
Box, not of A2).

**Scope=Instance — per-occurrence chain override, the sparse mode.** Each
target child contributes its **resolved object chain**: a link child
`Link001 → A1."Assembly2.Box."` registers `[A1, A2, Box]` (the link's own
root deliberately excluded), a plain child registers `[itself]` (= every
occurrence). The binding registry then enumerates the document's logical
occurrences — depth-first over every object's `getSubObjects()`, each
visited object expanded to its resolved sequence so occurrences *inside*
subname shortcuts still match the chain they resolve through — and every
occurrence whose resolved sequence **ends with** a registered chain
(suffix-anchored: `Assembly3.A1.A2.Box` matches, `A4.A2.Box` does not)
gets a per-path override. A match stops the descent: the override covers
the whole subtree, so an outer binding wins over any deeper match.
Occurrence overlap between Appearances: longest chain wins, then
`App::DocumentObject::TreeRank`, then name. Document signals (object
add/remove, link property changes) re-run the scan, coalesced through the
event loop and only while chains are registered.

The per-occurrence override rides the **per-path selection side channel**,
not a material merge: `getVertexCaches` memoizes one `vcachemap` per
render cache shared by every path through it, so a path-scoped override
cannot ride `Material::overrideflags` merge-down (that slot is per-cache,
path-blind). Instead `SoFCRenderCacheManager::addShaderOverride(key,
nodepath, shader)` mirrors the element-color-override machinery
(`selcaches`): a path-keyed sensor rebuilds a whole-object
`VertexCacheMap` for the bound instance path —
`SoFCRenderCache::buildWholeCacheMap`, which keeps the original geometry
and materials (normals intact; the highlight-index caches
`buildHighlightCache` substitutes carry none) — stamps `usershader` on its
triangle materials, and registers it as a **non-on-top whole-object
selection entry**: it renders in the normal scene passes with the shader
substituted while the base draws are suppressed by the whole-object key
(GL: `selectionkeys`/`draw_entry.skip`; bgfx: `hiddenKeys`). The backend
keeps the replaced geometry in the shadow-caster, depth-prepass,
debug-scene and ground-reflection passes (stock programs), so only the
beauty shading changes. Occurrence paths are materialized per 3D view
(`appendDetailPath` + `getDetailPath`).

**Scope=Element — face-level override.** As Instance, but a target
subname ending in a face element (`A1."A2.Box.Face3"`) restricts the
override to that face of every matched occurrence; a target without an
element part behaves like Instance. The element ref is appended to the
occurrence subname, so `getDetailPath` yields the tail shape's
`SoDetail`, and `buildWholeCacheMap` narrows the map to that face's
triangles out of the *original* cache (partial-index rendering — real
normals, unlike the highlight-index substitutes). The entry is partial,
so the base draw is **not** suppressed: the face renders over its
coincident base copy, with a small negative polygon offset (the
backend's NDC depth-bias emulation) so it wins the depth contest
regardless of draw order. Face elements only — edges and vertices have
no material stage to replace. Precedence: element-scoped bindings
coexist with a whole-occurrence winner and with each other (one winner
per element, the usual chain/TreeRank/name rule); whole bindings
register first so a face override draws over a whole-occurrence shader
on the same occurrence.

Scene-level activation is the same registry: a visible shader-only
Appearance contributes its Shader's `post`-stage programs (with its
parameter overrides applied) to a per-view list the renderer appends
after the node-captured shaders — `SoFCRenderer::setAppearanceShaders`,
forwarded through the render cache manager, replaced wholesale on every
binding rebuild and independent of scene recapture. Appending last makes
a document-object activation win the backend's "last shader on a stage"
rule over raw scene nodes; multiple shader-only Appearances order
ascending by TreeRank (name fallback), so the highest-ranked one wins,
matching the per-target precedence direction. A targeted Appearance
applies only object-scoped programs — post programs need a shader-only
group.

Found on the way (fixed): clearing a `PropertyXLink*` property to empty
never notified — `Property::hasSetValue`'s `isSame(_old)` optimization
compares live `getLinks()`, but a `copyBeforeChange()` snapshot of an
XLink stores only names (`copyTo`), so empty-after vs populated-before
compared "same". `PropertyXLink::isSame`/`PropertyXLinkSubList::isSame`
now compare name-level identity.

---

## 7. Implementation phasing

| phase | contents | depends on |
|---|---|---|
| 1 | **DONE** — `Render::RenderDebugConfig` + `translateRenderDebugConfig` + `u_debugParams`; `RenderDebug_ViewMode` modes 1–4 (existing targets only); `RenderDebug_FreezeFrame` | nothing — pure spine reuse |
| 2 | **DONE** — `saveRenderDump` Python API + sidecar JSON + `getRenderStats`; absorb `FC_BGFX_DEBUG_*` env gates; browser `dumpFrame` WS protocol + version-handshake/self `reload` (§4.4) | phase 1 (mode override) |
| 3 | **DONE** — verification harness (`scripts/render-verify.sh` + `render_verify.py` + `render_diff.py` + `wasm-hold.js`): named-view or golden-sidecar restaging, xvfb/`--gpu`/`--viewer` capture legs, first-divergent-stage diffing with heatmaps | phases 1–2 |
| 4 | **DONE** — dynamic name→uniform binding (`RenderDebug_*` props → like-named vec4 uniforms, snapshot v21) + `u_userParams[4]` fallback pool (lane 0 = debug output scale/bias); shader hot-reload (`FC_BGFX_SHADER_DIR` + `reloadShaders()`); `View3DInventor.addProperty/removeProperty` Python API | phase 1 |
| 5 | **DONE** — view modes 5–8 (ShadowTile coverage, Overdraw counting pass on the repurposed `ViewDebugScene` slot, ShadowFilter precision probe, UV re-render; snapshot v22); self-labeling burn-in (`RenderDebug_Label`) | 1, 4 |
| 6 | **first slice DONE** — user-loadable shaders on the Coin node model, `post` stage (coin fork: `SoShaderProgram::stage` + `BGFX_SC` source type; capture: cache-manager post-callback → `Render::UserShaderConfig` → `setUserShaderConfig`; backend: async shaderc compile cache + `ViewUserPostCopy`/`ViewUserPost` full-screen pass; sandboxed failure verified). **second slice DONE** — `material` stage with per-object attachment (`Material::usershader` through the render-cache chain, stock `vs_fc_mesh` pairing, beauty passes only, instancing exclusion). **third slice DONE** — browser tier (server-side compile through the async shaderc cache, snapshot v23 user-shader table, viewer loads shipped bins). **fourth slice DONE** — §6.5 document object model (App::ShaderProgram/Shader/Appearance + view providers, path-keyed shader overrides through the render cache manager) and §6.4 property-bound parameters (`Group_Name` dynamic props → SoShaderParameter nodes → uniforms with the consuming draws; Appearance per-binding overrides; stale-uniform zeroing). **fifth slice DONE** — scene-level Appearance activation (shader-only Appearance → `setAppearanceShaders` per view, TreeRank-ordered, wins over raw scene nodes). **sixth slice DONE** — Appearance reworked as an `App::LinkGroup` (children = shader + targets, `Scope` enum {Object, Instance}: direct merge-down attachment vs suffix-anchored per-occurrence chain overrides via the logical occurrence scan). **seventh slice DONE** — element-scoped targets (`Scope=Element`: face-level per-occurrence overrides through the same path channel, partial entries over the untouched base draw) and the new-view rebind hook (a 3D view created after the bindings exist gets the per-view registrations through a coalesced rebuild). **eighth slice DONE** — the material-stage lighting helper library (`fc_mesh_lighting.sh` core shared with the stock mesh shader, user-facing `fc_user_lighting.sh`, include-tree hash in the compile cache key). Phase complete | 4; shader compile cache (§6.3) |

Phases 1+2 are the minimum end-to-end slice: set a mode from Python, capture
a real-GPU frame with metadata, diff it.
