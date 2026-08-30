# TShape-Level Render Cache & Instancing

This document describes the design of TShape-level geometry sharing in
the rendering pipeline: how repeated occurrences of one OCCT
`TopoDS_TShape` are tessellated once, rendered as instances, colored
per instance without duplicating geometry, and collapsed into GPU
instanced draws by the bgfx backend.

It consolidates the design agreed and implemented in 2026-07
(`docs/RendererPlan.md` §3 "Instancing" carries the per-phase status
notes; this document is the architectural reference). **Status: the
workstream is complete** — leaf-level global table, color-variant
branching and CPU array dedup all shipped. Related reading:
`docs/RoadMap.md` (workstreams), `docs/ComputeBoundaries.md` (headless
engine design).

## 1. Problem

A CAD model is dominated by repetition: arrays of fasteners, patterned
features, assemblies referencing one part many times. OCCT already
expresses this — a `TopoDS_Shape` is a shared `TopoDS_TShape` plus a
`TopLoc_Location` — and `App::Link` expresses it at the document level.
But the classic visual pipeline flattens it away:

- `ViewProviderPartExt::updateVisual()` used to bake N transformed
  copies of the same tessellation into one coordinate node whenever a
  compound held located instances of one TShape.
- Every view provider tessellated its own shape even when the same
  TShape appeared in other objects.
- Per-face colors were baked into the flat arrays, so any color
  difference forced full geometry duplication.

The goals, in priority order set by the project direction (large-model
performance):

1. Tessellate each unique (TShape, orientation, tessellation quality)
   once, globally — across objects, not just within one compound.
2. Render repeated geometry as GPU instanced draws (one submit for N
   placements) whenever the backend supports it.
3. Let instances differ in color without breaking sharing, with the
   minimum number of draws (never trade draws for memory — worst case
   must equal the flattened build, not exceed it).
4. Never regress the plain Coin/GL path: without a capable backend the
   legacy flattened build runs unchanged.

## 2. Layering overview

The design spans four layers; each is independently useful:

```
PartGui  ViewProviderPartExt::buildInstanced()
         global _InstGeomTable: (TShape, orient, tess params) -> InstGeometry
         per-instance wrappers  SoSeparator[SoMatrixTransform, shared SoFCSelectionRoot]
         color variants: per distinct resolved face-color vector
   |
Gui      SoFCRenderCache / SoFCVertexCache (render-cache mode 3)
         entering one SoFCSelectionRoot under N transforms flattens into
         shared vertex-cache entries with per-instance matrices
   |
Bridge   SoFCRendererBridge: MeshData snapshots (positions/normals/
         colors/texcoords/indices as separate arrays, keyed by cacheId)
   |
bgfx     BGFXRenderer: instanced submits per group of same-cache draws;
backend  GPU buffers split into content-hash-shared GpuGeometry and
         per-cache color streams
```

A key property of the whole stack: **each layer shares by its own
key**. PartGui shares tessellations by TShape identity; the render
cache shares vertex caches by node identity; the backend shares GPU
buffers by array *content*. A failure to share at one layer (e.g. a
texture-state split of a vertex cache) is caught by the layer below.

## 3. The global instance-geometry table (PartGui)

`src/Mod/Part/Gui/ViewProviderExt.cpp` holds the global table:

```
InstGeomKey  { TShape*, orientation, quantized linear deflection,
               quantized angular deflection }
InstGeometry { face/edge/vertex SoFCSelectionRoot subgraphs,
               SoBrepFaceSet/SoBrepEdgeSet/SoBrepPointSet,
               shared coords/normal/texcoord nodes,
               color variants (see §5), element counts, refcount }
```

- **Instance unit = non-compound leaf sub-shape.** The compound is
  walked recursively (locations composed through nested compounds);
  each non-compound leaf is one instance. Face-level instancing was
  rejected: node explosion, low practical yield. A face table could
  nest inside leaf groups later if profiling ever demands it.
- **Entries are immutable.** A changed shape produces a new TShape and
  thus a new entry; entries are refcounted by the view providers'
  `ShapeInstanceRep` and erased at zero.
- **Tessellation runs in the leaf's local frame** with the deflection
  derived from the *local-frame leaf bounding box* (same formula as the
  flattened build, which uses the whole shape). Two rules matter:
  - The located bbox varies with instance rotation and would split the
    table key (symptom during bring-up: only accidental pairs shared).
  - The same part in differently sized parents must agree on one mesh,
    so the whole-shape bbox cannot participate. Different deviation
    settings key apart via the quantized parameters — that is intended.
- Solid classification for the section-cap pass is attached per entry
  in local part numbering (`SoFCShapeInfo`/`SoFCShapeInstance`).

### Instance wrappers

Each placed instance contributes three wrappers into the display-mode
roots (faces, edges, vertices kept strictly separate — see §5):

```
SoSeparator (render/bbox caching OFF)
 ├─ SoMatrixTransform  (the leaf's location)
 ├─ [SoMaterial]       (uniform-slice color override, faces only, §5)
 └─ shared SoFCSelectionRoot from the table entry (or a color variant)
```

The shared subgraph MUST be an `SoFCSelectionRoot`: it is the render
cache's child-cache boundary, and `addChildCache` resets the model
matrix inside. Plain `SoSeparator` wrappers were tried first and left
per-instance vertex caches (validity churn) — sharing silently never
happened.

## 4. Qualification: flatten unless provably better

The flattened build remains the default and runs whenever any of the
following holds (`buildInstanced()` gate):

- `ShapeInstancing` parameter off, or render-cache mode ≠ 3, or no
  backend renderer *live* (`Render::Renderer::activeCount()` — the
  actual instance count, not the Render Type preference: a backend can
  be attached with the pref still "Default", and a pref naming a
  backend yields none when creation fails), or the backend publishes no
  GPU-instancing capability (`Render::Renderer::instancingHint()`, from
  `BGFX_CAPS_INSTANCING`). Without real instancing many small shared
  nodes are a net loss — the 2021 `LinkShapeTable` lesson. A parameter
  observer rebuilds Part visuals when a gate parameter flips, and a
  renderer activity observer does the same when a backend comes or goes
  without a pref flip.
- The shape is not a compound, or no (TShape, orientation) repeats
  among its leaves.
- An instance is mirror-placed (`Trsf::IsNegative` flips winding).
- Two leaves are identical including location (`TopExp::MapShapes`
  would deduplicate the second and shift element numbering).
- The defensive numbering check fails: global FaceN/EdgeN/VertexN must
  equal the concatenation of the leaves' local numbering in traversal
  order (verified against `TopoShape::findShape` per leaf).
- The applied per-face *materials* diverge beyond diffuse+transparency
  (§5). Divergent per-element *colors* — faces, edges and vertices —
  never disqualify: they branch over color variants (§5).

**Color divergence is value-based, decided at apply time.** The
presence or length of a `DiffuseColor` array means nothing — a
same-valued array is uniform. The `setHighlighted*` apply points
inspect the final resolved values (including alpha) and restructure on
transitions in both directions; a divergence that becomes uniform again
re-qualifies lazily via `VisualTouched`.

## 5. Color variants (draw-minimal branching)

Baked per-part colors live in the vertex data of a cache (§6), so a
shared subgraph must never be traversed under differing per-face color
state. Divergent per-face colors therefore *branch below the shared
geometry* instead of flattening:

```
InstGeometry
 ├─ base faceGroup   (colors ride inherited material — the uniform variant)
 └─ variants[]       one per DISTINCT resolved per-face color vector
      ColorVariant { packed-RGBA key vector, refcount,
                     SoFCSelectionRoot [ shared coords/normals/texcoords,
                       SoMaterialBinding PER_PART,
                       SoMaterial (diffuse+transparency only; other
                         components setIgnored -> inherit),
                       own SoBrepFaceSet (copies index fields, shares
                         coordinate nodes) ] }
```

`applyInstancedFaceColors()` partitions the instances by their slice of
the resolved per-face vector:

- **Uniform slice** → stay on the base subgraph with a per-instance
  `SoMaterial` override in the wrapper. Uniform colors ride the
  render-cache material (the `App::Link` mechanism), so the vertex
  cache stays shared *whatever the color value* and the instanced
  submit carries the color per instance.
- **Divergent slice** → reference the variant matching the exact
  vector, lazily created, refcounted, shared by every instance across
  all objects applying that vector.

Consequences (the "draw-minimal" scenario analysis):

- Draws per TShape = number of distinct color vectors (+1 uniform
  group). Worst case — every instance a distinct vector — equals the
  flattened build's cost. There is **no face-level splitting ever**:
  splitting only trades draws for memory, and speed wins.
- New divergent instances materialize new variants; nothing already
  shared is re-partitioned.
- Variants are immutable: a different vector is a different variant.
  This keeps every cache in the system immutable after build.

**Line/point color variants** extend the same mechanism to edges and
vertices: `applyInstancedLineColors()`/`applyInstancedPointColors()`
partition the instances by their slice of the resolved
`LineColorArray`/`PointColorArray` (base line/point color filling a
short apply, like the flattened paths). A uniform slice rides a
diffuse-only per-instance override `SoMaterial` in the edge/vertex
wrapper; a divergent slice references a refcounted line/point variant —
own `SoBrepEdgeSet` (copies `coordIndex`/`seamIndices`) or
`SoBrepPointSet` (copies `startIndex`) under an `SoFCSelectionRoot`
sharing the geometry's coordinate node, with `PER_FACE` (one polyline
per edge) / `PER_VERTEX` diffuse binding and every other material
component inherited — lines and points never carry transparency, same
as the flattened per-element paths. Edge/vertex branching stays fully
decoupled from face branching; picking resolves variant line/point sets
exactly like variant facesets.

What still flattens: per-face *material* divergence beyond
diffuse+transparency (ambient/specular/emissive must stay uniform to
ride the inherited material).

## 6. Render cache flattening (Gui)

Nothing TShape-specific lives in `SoFCRenderCache`/`SoFCVertexCache` —
that is the point. The existing machinery provides the substrate:

- The vertex-cache table is keyed on the shape *node*. The same node
  revisited in a traversal yields `addChildCache` entries with
  per-traversal matrices — one shared `SoFCVertexCache`, N cache
  entries differing only in matrix (and material captured at the
  boundary). This is exactly what Link arrays always did; the instance
  wrappers reuse it below the object level.
- Per-part/per-vertex colors are baked into the vertex cache's color
  array; uniform colors/materials/textures/PBR properties ride the
  render-cache `Material` per entry and never bake. Hence the
  uniform/divergent split in §5.
- The cache-copy + `setFaceColors()` mechanism (used by Link
  element-color overrides and selection contexts) produces recolored
  caches sharing the underlying COW arrays — a second, independent
  source of "same geometry, different colors" caches that the backend
  dedups by content (§7).

One vertex-cache subtlety: a cache captures the
`SoMultiTextureEnabledElement`, so the same shared node traversed under
a *textured* consumer builds a second vertex cache (correct rendering,
split sharing). See §8 for why this stays cheap.

### CPU array dedup across variant caches

A variant shape node names its base shape node in a `protoNode` field
(`SoSFNode`, read by name like `forceTexCoords`). When the cache
manager builds a fresh vertex cache for such a node, it seeds it with a
cache of the prototype (the `prev` argument of the cache constructor).
The capture then runs through `SoFCVertexAttribute`'s
equality-preserving append: as long as the captured values equal the
seed's, the copy-on-write storage stays *shared*, and only the first
differing value detaches an array. Since a variant emits the same
primitives as its base over the same coordinate/normal/texcoord nodes,
the position, normal, texcoord and index arrays end up CPU-shared —
only the baked per-vertex color array is owned (colors necessarily
differ, that is what a variant is). The finished cache is also
registered under the prototype's cache-table entry (foreign entries
never pass the node-id check, so they act purely as seed candidates),
which keeps sharing alive even when the base node itself is never
traversed — e.g. every instance divergent — and lets sibling variants
and the base derive from whichever built first.

One inherent exception: *line* variants usually own their vertex
arrays. The capture deduplicates vertices through a hash keyed on the
full attribute tuple — position, normal, texcoord *and baked color* —
which is just the GL vertex model: an array entry is one tuple of all
attributes, so a point that needs two colors must exist twice (the
same reason a hard edge duplicates vertices for two normals). The
base lineset (uniform color) merges a corner touched by three edges
into one vertex; a variant giving those edges three colors produces
three entries. From the first split corner the arrays differ in
content and length, and the copy-on-write sharing is all-or-nothing
per array, so the variant owns its vertex and line-index arrays
outright. Since edges meet at their endpoints, any divergent coloring
of adjacent edges — which is what a divergent `LineColorArray` means
in practice — triggers this. Faces and points are unaffected: B-Rep
tessellation shares no coordinates across faces, and points are
unique anyway. The split also makes the variant's arrays hash apart
in the backend's geometry table (§7), so GPU sharing is lost for line
variants too — proportional only to edge data.

**Considered and rejected: per-edge color lookup.** Storing line
colors per *part* (one entry per edge polyline — what `LineColorArray`
semantically is) instead of expanding them per vertex would remove the
split entirely and restore both CPU and GPU sharing; the bgfx line
path even half-fits already (it bakes colors into per-segment instance
data, not a vertex stream). Rejected for now on cost/benefit:
the color-in-key capture lives in `SoFCVertexCache`, which serves
every shape, both renderers, selection subsets, merging and
transparency sorting; generic Coin scenes legitimately have
per-*vertex* colored lines, so the key change must be gated on the
material binding (a two-mode capture with a subtle correctness
matrix); the fixed-function GL path cannot fetch per primitive and
would need per-color-run draws or transient re-expansion; and the
payoff is kilobytes for a rare styling case — edge polylines are a
rounding error next to face meshes, and owning a few KB still beats
the flatten fallback it replaced. Revisit only if profiling shows
edge-dominated scenes paying for it; if so, scope it narrowly
(per-part storage for `PER_FACE`-bound line/point captures only, GL
expanding transiently, bridge reading the part table) behind an A/B
env.

**Selection and preselect do not duplicate.** Highlight and selection
overrides never re-run the capture — they derive from the *finished*
cache via the copy constructor (`prevattached` caches share every
array by refcounted copy-on-write and assert they never re-capture) —
so the vertex-splitting question never arises there. Whole-object
tint is a single uniform color and rides the entry `Material`
(binding forced to `OVERALL`; on a color-carrying cache the copy's
color array is *truncated*, a length reset on shared storage).
Partial selection copies share all vertex arrays and add only a
subset indexer plus the bridge-compacted index list. The one case
that bakes — divergent per-face selection colors via
`setFaceColors` — detaches the color array alone (4 B/vertex, the
same owned-colorarray cost a face variant pays). Selection contexts
also never touch the shared shape nodes (the `SoFCSelectionRoot`
stack mechanism), so no node id changes, no vertex cache rebuilds,
and the `protoNode` sharing stays intact across highlight churn.

Debug: `FC_DEBUG_VCACHE_PROTO=1` prints each fresh shape cache with its
node, prototype and array pointers (shared arrays show identical
pointers); `FC_NO_VCACHE_PROTO=1` disables the seeding for A/B runs.

## 7. Backend: instanced draws + content-shared GPU buffers (bgfx)

`src/Gui/Renderer/BGFXRenderer.cpp`.

### Instanced submits

`buildInstanceGroups()` groups draws at scene-set time by
(geometry **content hash**, index range, part index,
material-minus-diffuse, **resolved polygon-offset factor**); a frame
submits each group of >= 2 visible
members as **one instanced draw**: transient instance buffer of
`{model 4×vec4, diffuse vec4}` per instance, `vs_fc_mesh_inst` /
`vs_fc_mesh_tex_inst` selecting per-vertex color or the per-instance
diffuse by uniform.

- **Content-hash keying**: the key reuses `computeGeomKey`'s FNV hash
  plus a hash of the baked color stream, cached once per cacheId
  (`meshContents`; a cache id always refers to immutable content).
  Draws whose caches are merely *byte-identical* — imported
  duplicates, flattened copies without a shared TShape — batch exactly
  like shared-cache draws: the geometry table already gave them one
  set of GPU buffers, and equal color hashes make the prototype's
  color stream valid for every member.
- **The polygon-offset factor is keyed on, not just the material's
  half of it.** A fill's offset has to clear the DECORATION drawn over
  it, and that reach is not a material field -- `SoDrawStyle`'s line
  width lives in the wireframe separator `ViewProviderExt` adds after
  the faces, so every fill reports linewidth 1. `Private::decorReach`
  resolves it per object from the draw list. One instanced submit binds
  one `u_polyOffset`, its prototype's, so without this in the key two
  boxes of identical size whose edges differ in width shared an offset:
  the thick one lost the pull-back that keeps its edge from being
  half-eaten by its own face. Measured on two 20x20x2 plates, one at
  line width 12: the sink read 0.0005 model units against 0.33 for a
  26x26x2 neighbour, and a control built from identical shapes silently
  measured zero. Keying splits only the mismatched member -- four
  identical boxes at one width still batch as four, three when one is
  thickened (`scripts/fill_pullback_slope.py`, `FP_RED_SIDE`).
- **Textured draws batch**: the texture identity joins the group key
  (all five map ids — color/bump/emissive/occlusion/metallic-roughness
  — plus model/wrap/blend color and the texture matrix); the instanced
  submit routes through `vs_fc_mesh_tex_inst` and binds the texcoord
  stream + samplers exactly like the per-draw path (shared
  `bindTextureStage`).
- **Transparent draws batch on WBOIT frames**: the accumulation
  blending is commutative, so instance order is irrelevant —
  `vs_fc_mesh_inst`(+`_tex`) paired with the OIT fragment shaders,
  submitted to the transparent view with no depth write and no
  culling. On sorted-transparency frames (OIT resources missing or
  `FC_BGFX_DEBUG_NO_OIT`) `submitInstanced` refuses and the members
  fall back to per-draw depth-keyed submits. Transparent groups stay
  out of the SSAO/volumetric prepass like the per-draw path.

Eligibility: unclipped, non-on-top, non-water triangle draws of the
normal pass without autozoom; highlight frames keep per-draw submits
wholesale. Because grouping keys on content, PartGui's uniform group
and each color variant group batch naturally: draws per TShape =
#variants + 1, each a single instanced submit.

### GPU buffer sharing by content

GPU buffers are split in two:

```
GpuGeometry  keyed by GeomKey = FNV-1a content hash over positions/
             normals/texcoords/index arrays (+ counts):
             position+normal VB, tri/line/point/no-seam index buffers,
             outline instance data, texcoord stream
GpuMesh      keyed by cacheId:
             color stream VB (rgba8; absent when no baked colors),
             color-carrying line/point segment instance data,
             pointer into the GpuGeometry table
```

- Colors are a separate vertex stream (`Color0`); meshes without baked
  colors bind one shared all-white buffer, grown to the largest vertex
  count seen. Depth-only programs (prepass, shadow caster, water
  depth) bind the geometry stream alone — their vertex stages read no
  color attribute. Texcoords remain a lazily built third stream.
- The hash is computed once per cacheId appearance and is *content*
  identity, deliberately not a plumbed-through id: it dedups every
  producer of identical geometry — PartGui color variants (built as
  independent nodes, arrays equal by construction), the render cache's
  recolored copies, texture-state-split caches, and any coincidentally
  identical meshes — with zero coupling to the layers above.
- A color variant therefore costs 4 bytes per vertex on the GPU plus
  its material nodes; everything else is shared.
- Eviction: meshes age out by last-used frame; a geometry entry is
  dropped only after every referencing mesh is gone (meshes are
  collected first, so a surviving mesh has always refreshed its
  geometry's stamp).

Transient one-off draws (background gradient, shadow ground quad) keep
an interleaved position+normal+color layout — a separate stream would
buy nothing there.

## 8. Forced UV capture on shared tessellations

`SoFCVertexCache` captures unit-0 texture coordinates only while a
texture unit is enabled. For shared caches that is wrong twice:

- A Link-flattened consumer applies its texture to the *producer's*
  cache; if the producer was untextured at build time, the cache has no
  UVs and the texture samples one texel.
- Caches of one tessellation built under different texture states end
  up with different array sets and hash apart in the backend's
  geometry table.

Fix: `SoBrepFaceSet::forceTexCoords` (an ordinary field, read by name
from `SoFCVertexCache` like `elementSelectable`). When set,
`generatePrimitives` supplies the state's explicit unit-0 coordinates
even without an enabled texture unit (bypassing the texcoord bundle,
which is not set up in that case), and the vertex cache captures them.
PartGui sets the flag on the instance table's shared facesets and all
color variants, so every cache of a shared tessellation carries the
same UVs whoever builds it first. The B-Rep default-UV generator fills
the shared texcoord node at tessellation time, so forced capture always
has real coordinates to record.

The texture-state cache split of §6 thus becomes harmless: the second
cache re-shares the first one's GPU geometry through the content hash.

## 9. Element naming, picking, selection

- **Names stay exact.** Global `FaceN`/`EdgeN`/`VertexN` = per-instance
  base offset + index local to the shared node. `getElementPicked`
  resolves the picked instance from its wrapper separator on the pick
  path (`sepToInstance`), the shared node (or variant faceset) from
  `nodeToGeom`, and adds the instance's bases.
- **Per-instance sub-element highlight**: the instance wrappers are
  `SoFCSelectionRoot`, and selection/highlight contexts key on the
  traversed selection-root *stack* — so a context bound under one
  instance's wrapper highlights that instance alone over the shared
  shape nodes (the same mechanism that gives each `App::Link` its own
  contexts). `getDetailPath` resolves the picked global element to its
  instance, appends the graph chain down to that wrapper (found with a
  search across the display-mode roots — plain separators that never
  enter the context stack), and returns an `SoFC*Detail` with the
  LOCAL element index and the shared (or color-variant) shape node as
  context. Unresolvable cases (whole sub-shapes, foreign path tails)
  keep the previous whole-object degrade. Two supporting details: the
  bridge thickens the implicit whole-on-top companion lines of a
  partial selection (for flattened objects the partial id's own
  thickened copies shadow them in the backend dedup; the
  instance-scoped partial id has no copies of its own), and partial
  line/triangle subset caches now reach backends as real index
  subsets (see `getPartialLineParts` — the GL renderer consumed the
  indexer's partial part list, backends got the full array).
- The backend's whole-object selection dedup was made instance-aware:
  the dedup key includes a model-matrix hash (distinct placements are
  not duplicates) and explicitly colored selection entries win over the
  implicit untinted whole-on-top copy (`SelIdBits` mirrored into
  `Renderer.h`).

## 10. Known deviations & limitations

- **Leaf-scale default UVs.** The flattened build derives default UVs
  from the whole shape's bbox; a shared tessellation can only use the
  leaf's. Instanced compounds therefore tile textures at leaf scale.
  Inherent to sharing (and arguably the more useful behavior for
  instanced parts).
- **Shadow-frame edge lines.** Per-instance model matrices vs
  flatten-baked coordinates differ at float precision; the binary
  shadow-map test amplifies this into a ~1k-px thin-edge-line diff
  class on shadowed edges. Plain shading compares 0 px.
- **Line variants own their CPU vertex arrays.** Face and point
  variant caches CPU-share the base cache's geometry arrays through
  the `protoNode` seeding (§6); divergent per-edge colors split shared
  corner vertices, so a line variant's arrays genuinely differ.
- Divergent line/point colors and per-face materials beyond
  diffuse+transparency still flatten (§5).
- Same-valued color arrays applied as arrays take their transparency
  from the array's alpha (flat-path semantics preserved).

## 11. Verification approach

Every phase was verified on llvmpipe under Xvfb with the
`smoke_inst.py` recipe (see the session memory for envs): the invariant
is **instanced vs flattened = 0 px** — for plain scenes, divergent
color scenes (shared variant + override + base mix), whole-object
selection, and color transitions in both directions — plus scene-graph
probes (matrix-transform counts: GL/Default stays flattened) and
`FC_BGFX_DEBUG_FEED` assertions on instanced-group coverage
("N groups covering M draws") and geometry-hash sharing
("bgfx mesh cache=… geom=… shared"). The full phase A+B pixel matrix
was re-run bit-identical after the phase C backend refactor.

## 12. Future work

- Occlusion culling / GPU-driven paths for very large assemblies
  (RendererPlan Phase 3 rows; instanced shadow/prepass submits, CPU
  frustum culling, line/point variants and per-instance highlight are
  done).
- Textured / OIT-transparent instanced submits.
- Cross-layer idea now cheap to explore: instance groups could key on
  the backend's *geometry hash* instead of the cache pointer, batching
  coincidentally identical flattened objects too.
