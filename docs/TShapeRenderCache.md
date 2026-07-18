# TShape-Level Render Cache & Instancing

This document describes the design of TShape-level geometry sharing in
the rendering pipeline: how repeated occurrences of one OCCT
`TopoDS_TShape` are tessellated once, rendered as instances, colored
per instance without duplicating geometry, and collapsed into GPU
instanced draws by the bgfx backend.

It consolidates the design agreed and implemented in 2026-07
(`docs/RendererPlan.md` §3 "Instancing" carries the per-phase status
notes; this document is the architectural reference). Related reading:
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
  backend renderer selected, or the backend publishes no GPU-instancing
  capability (`Render::Renderer::instancingHint()`, from
  `BGFX_CAPS_INSTANCING`). Without real instancing many small shared
  nodes are a net loss — the 2021 `LinkShapeTable` lesson. A parameter
  observer rebuilds Part visuals when a gate parameter flips.
- The shape is not a compound, or no (TShape, orientation) repeats
  among its leaves.
- An instance is mirror-placed (`Trsf::IsNegative` flips winding).
- Two leaves are identical including location (`TopExp::MapShapes`
  would deduplicate the second and shift element numbering).
- The defensive numbering check fails: global FaceN/EdgeN/VertexN must
  equal the concatenation of the leaves' local numbering in traversal
  order (verified against `TopoShape::findShape` per leaf).
- The applied line/point colors diverge in value, or per-face
  *materials* diverge beyond diffuse+transparency (§5).

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

What still flattens: per-face *material* divergence beyond
diffuse+transparency (ambient/specular/emissive must stay uniform to
ride the inherited material), and divergent line/point color arrays.
Edge/vertex rendering is fully decoupled from face branching — their
wrappers always reference the base line/point subgraphs; a variant
mechanism for them is a possible follow-up.

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

## 7. Backend: instanced draws + content-shared GPU buffers (bgfx)

`src/Gui/Renderer/BGFXRenderer.cpp`.

### Instanced submits

`buildInstanceGroups()` groups draws at scene-set time by
(mesh pointer, index range, part index, material-minus-diffuse); a
frame submits each group of ≥ 2 visible members as **one instanced
draw**: transient instance buffer of `{model 4×vec4, diffuse vec4}`
per instance, `vs_fc_mesh_inst` selecting per-vertex color or the
per-instance diffuse by uniform. Eligibility: opaque, untextured,
unclipped, non-on-top, non-water triangle draws of the normal pass; all
other passes (prepass, shadow caster, water depth, highlight frames)
keep per-draw submits. Because grouping keys on the shared cache,
PartGui's uniform group and each color variant group batch naturally:
draws per TShape = #variants + 1, each a single instanced submit.

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
- **Sub-element highlight degrades to whole-object highlight**: a
  detail context binds to the shared node and would light the element
  in *every* instance, so `getDetailPath` degrades instead. Reported
  names remain exact. Per-instance highlight contexts (path-keyed) are
  a known follow-up.
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
- **CPU arrays are not deduped across variants.** Variant facesets
  build their own (equal) CPU arrays; only the GPU side shares. A
  CPU-side dedup (deriving variant caches from the base cache via the
  copy + `setFaceColors` mechanism) is a possible follow-up.
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

- Instanced shadow-caster and prepass submits (members currently keep
  per-draw side submits).
- Frustum culling per instance; GPU-driven paths for very large
  assemblies (RendererPlan Phase 3 rows).
- Line/point color variants; per-instance sub-element highlight.
- CPU-side array dedup across variant caches.
- Textured / OIT-transparent instanced submits.
- Cross-layer idea now cheap to explore: instance groups could key on
  the backend's *geometry hash* instead of the cache pointer, batching
  coincidentally identical flattened objects too.
