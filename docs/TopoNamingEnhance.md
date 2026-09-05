# Topological Naming: what is left to build

Status: design and evaluation, written 2026-09-05 against this fork at
`d6435599a2`.  Nothing here is built yet.

The topological naming problem itself is solved in this fork -- element
maps, mapped names, the string hasher and the history-walking recovery
have shipped for years, and `CLAUDE.md` correctly says it is no longer
the active focus.  This document is about two things that were never
finished on top of it:

1. **A pre-task**: every element reference in a document is stored as a
   name and nothing else.  The geometry it was made against exists only
   in memory, for the duration of one session.  Now that shape storage
   is content-addressed and deduplicated, that geometry can be *named*
   in the file for almost nothing, and a reference that breaks can then
   be shown to the user instead of silently repointed or silently lost.
   Section 2.
2. **The main task**: TechDraw does not use element maps at all.  Not
   partially -- at all.  Section 3.

Related reading: `docs/SharedShapeStorage.md` (the content-addressed
blob store the pre-task rides on, especially sec 12.1 to 12.3),
`docs/FileBlobsManager.md` sec 13 (the blob index format),
`docs/TechDrawPortAndSection.md` (the upstream port, now complete, and
the section-view work the main task builds on).

Probe scripts for everything measured here were kept in the session
scratchpad, following the `DrawBrokenView` convention of not committing
them.  **Section 6.3 restates the three models so every number here can
be reproduced without them.**

**Resuming?  Start at section 6.**  It carries the state of play, the
five decisions that are still open, and the first thing to build.


## 1. The short answers

1. **Is there already a base-shape reference internally?**  Yes.
   `Part::Feature::_elementCache` (`src/Mod/Part/App/PartFeature.cpp:1206`)
   snapshots the referenced geometry immediately before a shape changes,
   and `searchElementCache` uses it to re-find the element afterwards.
   It is real, it works, and it is **never written to the file**.  It
   also holds only the sub-shape, not the base shape, so the recovery it
   can do is exact geometric equality and nothing better.

2. **Would storing it be expensive?**  No, and this is the measurement
   that makes the pre-task worth doing now rather than two years ago.
   A shape is already one content-addressed blob named after the
   property that owns it, listed in `blobs/Content.xml` with the list of
   every referrer that shares it.  A reference that records the base
   shape it was resolved against records a **40-character hash**, and in
   the healthy case that hash is already in the file.  Section 2.5.

3. **Is the existing recovery actually broken?**  Less than expected,
   and the honest framing matters.  Measured, it repairs more than it
   fails at -- but it does so **silently, automatically, with no record
   and no way for the user to check it**, and when it does fail it fails
   to `?Face3` with the original geometry gone forever.  The pre-task is
   about making repair *reviewable*, not about making it possible.
   Section 2.3.

4. **Can TechDraw carry element names through HLR?**  Yes, and more
   cheaply than assumed.  Both OCCT HLR paths expose the source edge of
   every projected edge through public API: the exact path through
   `HLRBRep_Data::EDataArray()` / `EdgeMap()`, the polygon path through
   the `TopoDS_Shape&` that `HLRBRep_PolyAlgo::Hide` / `Show` fill in.
   No geometric correlation, no second HLR pass, no OCCT patch.
   Section 3.5.


## 2. Pre-task: store the base shape with every element reference

### 2.1 The recovery ladder that exists today

Three layers, in the order they are consulted:

| layer | mechanism | where |
| --- | --- | --- |
| 1. mapped name | `GeoFeature::resolveElement` against the stored shadow name | `PropertyLinks.cpp:353` |
| 2. geometric search | `searchElementCache` -> `TopoShape::searchSubShape` against a snapshot of the old sub-shape | `PropertyLinks.cpp:382` |
| 3. history guess | `Part::Feature::getRelatedElements` -> `getElementSource`, longest common ancestry in the element map | offered by PartDesign's Gui, not applied automatically |

Layer 1 is the topological naming fix and does the overwhelming
majority of the work.  Layer 3 is only ever *offered*: PartDesign's
`Utils.cpp:1727` warns and populates a list of candidates for the user
to pick from.  Layer 2 is the interesting one, because it is fully
automatic and it is the only layer that consults geometry.

### 2.2 Where the base-shape reference already lives

Layer 2's evidence is gathered in `Feature::onBeforeChange`
(`PartFeature.cpp:1226`).  Before the `Shape` property changes, the
feature walks every property that holds an element reference to it
(`App::PropertyLinkBase::getElementReferences(this)`), and for each
referenced element it stores the sub-shape:

    res.first->second.shape = propShape->getShape().getSubTopoShape(
            element + (prefix?prefix->size():0), true);

into

    struct Feature::ElementCache {
        TopoShape shape;
        mutable std::vector<std::string> names;
        mutable bool searched;
    };

`searchElementCache` (`PartFeature.cpp:1354`) then runs
`newShape.searchSubShape(it->second.shape, &names, options, tol, atol)`
on the new geometry, and if that finds nothing it falls back to "if the
new shape has exactly one sub-shape of that type, take it".

Four properties of this cache decide the whole design:

- **It is a sub-shape, not a base shape.**  One face, detached from
  everything around it.  There is no adjacency, no neighbourhood, no
  element map, and no way to say what the face was *part of*.
- **It is in memory only.**  No `Save`, no `Restore`, no property.  It
  is rebuilt from scratch on every shape change and dies with the
  document.
- **It is gathered opportunistically.**  Gated on
  `!getDocument()->testStatus(App::Document::Restoring)` and
  `!getDocument()->isPerformingTransaction()`, and it can only see
  referrers whose owning document is loaded.  A cross-document XLink
  from a document that is not open contributes nothing.
- **It is one generation deep.**  The next shape change clears it and
  re-seeds it from whatever the references say *now*, broken or not.

So the base shape is not merely unstored: it was never gathered.  The
snapshot is taken at exactly the right moment and then thrown away along
with all of its context.

### 2.3 What the ladder can and cannot do, measured

Model: a 20x20x20 `Part::Box` cut by a radius-3 `Part::Cylinder`, and a
`Part::Plane` attached `FlatFace` to one face of the `Part::Cut`.  The
reference is broken by replacing the Cut's `Base` with a **new** box
object 25 tall, so the old element's source tag no longer exists and
layer 1 cannot resolve.  Three picks, same edit (`probe_fix3.py`):

| picked face | before | after the edit | outcome |
| --- | --- | --- | --- |
| `Face1` (side) | area 400.00, centre (0, 10, 10) | area 500.00, centre (0, 10, 12.5) | repaired, silently |
| `Face6` (side) | area 400.00, centre (20, 10, 10) | area 500.00, centre (20, 10, 12.5) | repaired, silently |
| `Face3` (top, holds the hole) | area 371.73, centre (10, 10, 20) | -- | **`?Face3`**, reference lost |

Two things to take from this, and they pull in opposite directions.

**The recovery is better than it looks on paper.**  The two side faces
changed area (400 -> 500) and moved their centre, and were still
matched: `searchSubShape` is comparing the underlying surface, not the
bounded face, so a face that grows on a plane that stayed put is found.
That is a good design and it is why this fork's references survive
ordinary editing.

**When it fails, everything is gone at once.**  `Face3` sat on the plane
`z = 20` and the edit moved that plane to `z = 25`.  Layer 1 fails (new
tag), layer 2 fails (different plane), and the end state is:

    <Exception> Attacher.cpp(865): AttachEngine3D: subshape not found Cut.?Face3

a property whose value is the literal string `?Face3`, an exception in
the report view, a `Part::Plane` stranded at its old placement, and
**no surviving description of what `Face3` was**.  It was a 371.73 mm^2
planar face at `z = 20` with a 28.3 mm^2 hole in it one recompute ago.
Nothing in the document says so.

**And the successes are as unaccountable as the failure.**  `Face1` and
`Face6` were repaired with no user-visible event at all -- in that run
not even the `FC_WARN "auto change element reference"` line fired,
because the *indexed* name `Face1` still parsed and layer 1 accepted it.
The reference now points at a face that happens to be correct.  Nothing
in the document records that a substitution happened, what the old
geometry was, or that the user might want to check it.  On a model where
the index happens to land on the wrong face, this is exactly the failure
mode `37cb56d3a0` had to fix in TechDraw's dimension references: a
plausible wrong answer is worse than a visible missing one.

> The pre-task is not "make broken references recoverable".  It is
> "make every repair, successful or not, something the user can see and
> overrule" -- which requires keeping the evidence, which requires
> storing it.

### 2.4 What a stored base shape buys that a stored sub-shape does not

Storing the old *sub-shape* would already fix the durability half.  It
is worth being explicit about why the base shape is the right unit:

- **The element map comes with it** -- *only if it is stored as a shape
  property rather than as a bare content hash.  Corrected in sec 5.2;
  the blob a hash names is geometry alone.*  A stored base shape carries
  its `<ElementMap2>` block, so the old mapped name can be looked up in
  the old map, and layer 3's ancestry walk becomes available across a
  reload rather than only while every ancestor object still exists.
- **Adjacency and neighbourhood.**  "The face between these two edges",
  "the face that shared an edge with the one that is still there" are
  answerable from a base shape and unanswerable from a detached face.
  This is the ingredient that would let `handleNoExactMatch`-style logic
  finally be written -- on both sides of the fork the phase-2 similar
  matching stopped exactly here, for want of context.
- **It is showable.**  The point of the whole exercise: a repair dialog
  that renders the old shape with the picked element highlighted, next
  to the new shape with the proposed element highlighted.
- **It costs the same as the sub-shape, or less.**  This is the part
  that only became true recently -- see below.

### 2.5 Why it is cheap now, measured

The premise, from `docs/SharedShapeStorage.md` sec 12.3: since
2026-08-17 `PropertyPartShape` is an ordinary blob referrer.  Its
geometry is one file addressed by content, skipped when unchanged,
pruned when orphaned, and listed with **every property that refers to
it**.

Measured on the probe model (`probe_ref.py`), a schema-5 save:

    Document.xml                        raw=20572   stored=3095
    blobs/Content.xml                   raw=608     stored=319
    blobs/Box.Shape.brp                 raw=1915    stored=531
    blobs/Box.ShapeMaterial.FCMat       raw=675     stored=329
    blobs/Cut.Shape.brp                 raw=3462    stored=825
    blobs/Cyl.Shape.brp                 raw=962     stored=370
    blobs/Plane.Shape.brp               raw=640     stored=262

and the index:

    <F n="Box.Shape.brp"           h="2ce295d0..." r="2965:Box.Shape"/>
    <F n="Box.ShapeMaterial.FCMat" h="1e39ec8b..." r="2965:Box.ShapeMaterial 2966:Cyl.ShapeMaterial 2967:Cut.ShapeMaterial 2968:Plane.ShapeMaterial"/>
    <F n="Cut.Shape.brp"           h="9e110747..." r="2967:Cut.Shape"/>
    ...

The material line is the proof of the mechanism: **four properties, one
file, one hash, four referrer tokens.**  Multi-referrer content sharing
is not a thing that would have to be built for this; it is what the
store already does, and it is what a link property joining the referrer
list would use unchanged.

The shape property already records its blob by hash, inline:

    <Property name="Shape" type="Part::PropertyPartShape">
      <Part HasherIndex="0" ElementMap="1.15.80001.4"
            hash="9e1107472c246e99c9c300d9a3af445b0e595408"/>
      ...

### 2.6 The format: one attribute

Measured, this is exactly what a link sub-element serializes today
(`probe_ref3.py`, the `Part::Plane`'s `AttachmentSupport`):

    <Property name="AttachmentSupport" type="App::PropertyLinkSubList">
      <LinkSubList count="1">
        <Link obj="Cut" sub="Face1" shadow=";Face1;:H87a,F.Face1"/>
      </LinkSubList>
    </Property>

`obj`, `sub`, `shadow`.  The proposal is one more attribute:

    <Link obj="Cut" sub="Face1" shadow=";Face1;:H87a,F.Face1"
          base="9e1107472c246e99c9c300d9a3af445b0e595408"/>

naming the blob the reference was last successfully resolved against.
An older reader ignores an unknown attribute, so this is additive within
schema 5; it is still gated on schema 5 because that is where the store
exists at all.

`PropertyLinkSub`, `PropertyLinkSubList`, `PropertyXLinkSub` and
`PropertyXLinkSubList` all serialize `<Link .../>` through the same
shape of code, so the attribute lands in one place per class, next to
the existing shadow handling.

The property becomes an `App::BlobReferrerProperty`
(`src/App/FileBlobManager.h:138`) -- the interface `PropertyFileIncluded`
and `PropertyPartShape` already implement -- so that `collectBlobs`
notes the hash and keeps the blob alive, and `assignRestoredBlob`
receives it back.

### 2.7 The cost when it is not free, and the retention policy

The honest accounting.  Recording the hash of a shape the file already
holds is free.  But the case the feature exists for is precisely the
case where the referenced object *has* changed, and then the old blob
has to be **retained** rather than pruned -- content the file would
otherwise not carry.

Three things bound that, and they should be built in from the start:

1. **One generation per (object, property), not per reference.**  Every
   link into `Cut.Shape` shares one base blob.  A model with 200
   references into 20 features retains 20 shapes, not 200.
2. **Retention is conditional on breakage.**  While every reference into
   an object resolves cleanly through layer 1, the base attribute can be
   rewritten to the *current* hash on save, and the old blob falls out
   of the referrer list and is pruned by the existing orphan sweep.  The
   file only grows for objects that actually have an unrepaired or
   recently repaired reference.
3. **A ceiling.**  A per-document cap, and a preference to disable the
   whole thing, so that a pathological history cannot grow a file
   without limit.

Sizing, from the probe: `Cut.Shape.brp` is 3462 raw / 825 stored.  For a
real assembly, `docs/SharedShapeStorage.md` sec 11.4 measured
`scanner.FCStd` at 875496 bytes of collapsible geometry across 36
objects, i.e. tens of kilobytes per retained generation.  That is the
number the ceiling has to be set against, and it is not yet measured for
this feature.

**Open question, not settled here.**  Whether the retained generation
should be the whole base shape or the base shape *pruned to the
neighbourhood* of the referenced elements (the referenced sub-shapes
plus their adjacent faces, as one shell).  Pruning would cut the cost by
an order of magnitude on large parts and keeps everything section 2.4
asked for except the complete element map.  It also costs a new shape
that is not identical to any shape already in the file, which forfeits
the free case of 2.5.  This wants measuring on a real assembly before it
is decided; the build order below does the whole shape first because it
is the one that is provably free while healthy.

### 2.8 What it enables, in order of value

1. **A repair dialog with two pictures.**  Old shape, picked element
   highlighted; new shape, proposed element highlighted; accept, pick
   another, or leave broken.  This is the friendlier reference fix the
   whole pre-task is for, and it is impossible today because the left
   picture does not exist.
2. **A repair record.**  A silent automatic repoint becomes a listed
   change the user can review after a recompute, like a merge conflict
   list.  Section 2.3 showed that today a *successful* repair is as
   invisible as a failed one.
3. **Layer 2, durably.**  The geometric search stops depending on having
   witnessed the change live, so it works after a reload, after an undo,
   through a transaction, and for a cross-document link whose other
   document was closed at the time.
4. **Layer 3, across a reload.**  The old element map travels with the
   old shape, so ancestry matching no longer needs every ancestor object
   to still exist in its old form.
5. **The phase-2 matching neither fork ever wrote.**  Both upstream's
   `findSimilar*` stubs and this fork's `handleNoExactMatch()` stopped
   for want of context about what the old element was part of.

### 2.9 Build order for the pre-task

- **P1. Persist the hash.**  Add `base=` to the four link classes'
  `<Link>` serialization, populate it wherever `shadow` is populated,
  make the properties blob referrers so the blob is kept and restored.
  No behaviour change: nothing reads it yet.  Gate:
  `src/Mod/Test/ShapeStorage.py` extended -- the attribute survives a
  round trip, a healthy save rewrites it to the current hash, an
  orphaned generation is pruned.
- **P2. Feed layer 2 from it.**  `Feature::searchElementCache` falls
  back to the stored base shape when `_elementCache` has no entry, which
  is every path listed in 2.2 as opportunistic.  Still automatic, still
  silent -- but now it works after a reload.  Gate: the `probe_fix3.py`
  matrix, run in a second process against a saved file.
- **P3. Record repairs.**  A per-document list of substitutions made
  during a recompute, with the old and new mapped names and the old base
  hash.  No dialog yet; a Python accessor and a report-view summary.
  This is what turns 2.3's silent success into something checkable.
- **P4. The dialog.**  Two viewports, old and new, driven by P3's list.
- **P5, optional, gated on measurement.**  Neighbourhood pruning (2.7),
  if the retention cost on a real assembly says the whole shape is too
  much.

P1 and P2 are the whole functional change.  P3 and P4 are the part the
user actually sees, and are the reason to do it.


## 3. Main task: TechDraw adopts element names

### 3.1 The finding

    $ grep -rn "ElementMap|MappedName|IndexedName|getElementName" src/Mod/TechDraw/
    (0 matches)

TechDraw has no element-map awareness of any kind.  The module is
`TopoDS_Shape`-based end to end and every identity in it is positional.

### 3.2 Where the map is dropped

`ShapeExtractor::getShapes` (`src/Mod/TechDraw/App/ShapeExtractor.cpp:96`)
is the entry point for all view geometry.  Line 105:

    auto shape = Part::Feature::getShape(l);

`Part::Feature::getShape` returns a `TopoDS_Shape`.  The named variant
`Part::Feature::getTopoShape(obj, ..., noElementMap=false)` is declared
on the next line of `PartFeature.h` and is never called from TechDraw.
The shapes are then rebuilt into a raw `BRep_Builder` compound (line
114), and `getShapesFused` fuses with a bare `BRepAlgoAPI_Fuse` (line
195).

So the element map is discarded before the first projection, in the
first function.  Everything downstream is a consequence of that one
line.

### 3.3 The three positional identity schemes

| what | identity | where | how it breaks |
| --- | --- | --- | --- |
| projected edges | `TopExp_Explorer` order over each HLR category compound, categories appended in a fixed sequence | `GeometryObject.cpp:572` | any model edit, and a `ScrubCount` change, renumbers everything |
| projected vertices | order of first appearance while walking edges, deduplicated by position | `GeometryObject.cpp`, `addGeomFromCompound` | one new edge renumbers every later vertex |
| faces | wires **sorted by area** | `DrawViewPart.cpp:584`, `EdgeWalker::sortWiresBySize` | two faces cross in area and every hatch on them swaps |

`BaseGeom` reserves the slot for a 3D backlink and never fills it:

    int ref3D;                      //obs?
    ...
    int ref3D;                      //obs. never used.

The consumers that persist one of those three schemes:

| holder | stores |
| --- | --- |
| `DrawViewDimension::References2D` | `PropertyLinkSubList` of `"Edge5"` / `"Vertex2"` |
| `DrawViewDimension::References3D` | `PropertyLinkSubList` into the source model -- **could be topo-named today** |
| `DrawHatch::Source`, `DrawGeomHatch::Source` | `PropertyLinkSub` holding `"Face3"` |
| `CosmeticVertex::linkGeom` | an int; the comment reads `//connection to corresponding "geom" Vertex (fragile - index based!)` |
| `GeomFormat::m_geomIndex` | an int; `Cosmetic.h:161`, `//connection to edgeGeom` |
| `CenterLine::m_faces / m_edges / m_verts` | 2D name strings |
| `DrawViewDimExtent::Source` | `PropertyLinkSubList` of 2D edges |

What holds the line today is `DrawViewDimension`'s own `SavedGeometry`
(`Part::PropertyTopoShapeList`) plus `GeometryMatcher` -- the same shape
of idea as `_elementCache` in section 2, built independently inside
TechDraw, and persisted where the core one is not.  It recovers geometry
that did not change.  It cannot follow an element through an edit, and
`handleNoExactMatch()` still carries the comment "this is where we
insert the clever logic".

### 3.4 Sections, which are worse for a structural reason

Two stacked instabilities:

1. **The cut is anonymous.**  `DrawViewSection` cuts with raw
   `BRepAlgoAPI_Cut` (`DrawViewSection.cpp:583`, `593`, `617`), and
   `DrawComplexSection` the same.  Even a named input would come out
   unnamed.  The fork's `TopoShape::makEBoolean("Cut", ...)`, which
   propagates names through the `Generated` / `Modified` history, is
   right there and unused.
2. **The cut face has no name to inherit.**  The section faces are
   *created by* the cut, from `makeCuttingTool`
   (`DrawViewSection.cpp:715`), a prism built ad hoc with no element map
   of its own.  A correct `makEBoolean` would still name the new faces
   off an unnamed tool.

So the cut surface -- the thing users hatch, colour, dimension against,
and which this fork additionally renders into the shaded underlay
(`ShadedUnderlay.cpp`) and streams to the browser (`PageFeed.cpp`,
`PageServe.cpp`) -- is identified by `alignSectionFaces()` output order
into `m_sectionTopoDSFaces`, then by area-sorted index in
`m_tdSectionFaces`.  Nudge `SectionOrigin` and every reference to it is
a coin flip.  `DrawViewDetail` inherits the same problem through
`BaseView`, and `UsePreviousCut` chains it further.

This is where the fork has invested most
(`docs/TechDrawPortAndSection.md` sec 31: stored `m_cutFrame` /
`m_preparedFrame`, prepared-space capture, per-face colours), which
makes the unstable identity underneath it the load-bearing weakness.

### 3.5 The enabler: HLR already knows the source edge

This is the result that decides the cost of the whole main task, and it
is better than a geometric correlation scheme.

**Exact HLR.**  `HLRBRep_HLRToShape::InternalCompound` walks
`DS->EDataArray()` by index `ie` and calls a private `DrawEdge(...)` per
edge.  `HLRBRep_Data::EDataArray()` is indexed identically to
`HLRBRep_Data::EdgeMap()`, an `NCollection_IndexedMap<TopoDS_Shape>` of
the **original** edges.  Every accessor that traversal needs is public:

    HLRBRep_Algo : public HLRBRep_InternalAlgo   // DataStructure() is public
    HLRBRep_Data::EDataArray()   EdgeMap()   FaceMap()
    HLRBRep_Data::NbEdges()      NbFaces()   Projector()
    HLRBRep::MakeEdge()          MakeEdge3d()
    HLRAlgo_EdgeIterator

So an in-tree reimplementation of `InternalCompound` -- roughly 60 lines
-- emits the same edges in the same order **and reports `ie` with each
one**.  That is the source edge, exactly, with no matching heuristic and
no second HLR pass.

**Polygon HLR** (the `CoarseView` path, `HLRBRep_PolyAlgo`) is easier
still: its public iteration hands back the source shape directly.

    HLRAlgo_BiPoint::PointsT& Hide(HLRAlgo_EdgeStatus& status,
                                   TopoDS_Shape& S, bool& reg1, ...);
    HLRAlgo_BiPoint::PointsT& Show(TopoDS_Shape& S, bool& reg1, ...);

`S` is filled with the shape the segment came from, per segment.

**Trap, if the two-pass route is taken instead.**  `DrawEdge`'s only
difference under `In3d` is `MakeEdge` versus `MakeEdge3d` over the same
`(sta, end)` interval -- the selection and the traversal are identical,
so 2D and 3D results do correspond one to one.  But both makers are
followed by `if (!E.IsNull())`, and nothing guarantees the two agree on
nullity for a degenerate interval.  The reimplemented traversal has no
such gap and is the better route.

### 3.6 Build order for the main task

Each step is independently useful and independently shippable.

**T0. Stop discarding the map.**  `ShapeExtractor` returns
`Part::TopoShape` via `getTopoShape`; fuse with `makEBoolean("Fuse")`;
compound with `makECompound`.  Purely mechanical, nothing consumes the
names yet.  This alone makes `References3D` topo-nameable, which fixes
the model-facing half of dimension breakage with no HLR work at all.

**T1. Name the cutting tool, then cut with names.**  Give
`makeCuttingTool()` a hand-built element map with fixed names for its
faces (`SectionPlane` and the box sides), then switch the cut to
`makEBoolean("Cut")`.  Section faces then carry stable generated names
derived from `SectionPlane`, and stay stable as the model changes.  Same
treatment in `DrawComplexSection`.  **This is the highest-value change
for section stability and it is contained to two functions.**

**T2. Carry names through HLR.**  Replace the `HLRToShape` calls in
`GeometryObject::projectShape` with the in-tree traversal of 3.5, so
every `BaseGeom` learns its source element.  Store the mapped name on
`BaseGeom` where `ref3D` is reserved.

Give each projected edge a stable name of the form

    <sourceElementName>;HLR:<class>:<ordinal>

where `class` is the `edgeClass` plus visibility and `ordinal`
disambiguates one source edge split into several fragments by hiding.
Vertices name from the edges that meet at them; faces name from the
wires that bound them, which kills the area-sort fragility outright.

**T3. Migrate the consumers.**  `References2D`, `DrawHatch::Source`,
`GeomFormat::m_geomIndex`, `CenterLine`, `CosmeticVertex::linkGeom`.
Each keeps its index for display and gains a stable name for
persistence.  `SavedGeometry` is demoted to the fallback it should be --
consulted when the name resolves to nothing, not as the primary
mechanism.  The recovery ladder becomes *name -> exact geometry ->
similar geometry -> ask the user*, where today it is only the middle
rung, and the last rung is what the pre-task's stored base shape makes
possible.

**Sequencing.**  T0 and T1 together are a small self-contained change
that fixes sections without touching HLR.  Do those first and measure
before committing to T2, which is the only genuinely hard piece.

### 3.7 Traps, all of them already paid for in this tree

- `DrawViewDimension::execute()` runs while the document is still
  restoring, against a view that has not projected yet.  A migration
  keyed on "have I run once" spends its one chance on that pass and does
  nothing.  A migration must report whether it could *evaluate*, not
  merely whether it ran.  (`docs/TechDrawPortAndSection.md` sec 34.3.)
- `SavedGeometry`'s stored frame negates Y relative to the Python
  accessors.  A test built from accessor values without the flip matches
  neither frame and looks like a broken fix.  (Sec 34.2.)
- Every ported or reworked Gui item has a second renderer to satisfy:
  the Qt `QGraphicsScene` and the vg feed through `PageFeed`, and
  `DrawViewPart` descendants additionally answer to `ShadedUnderlay`.
  T3 touches only App-tier persistence and should stay clear of both; if
  it does not, that is the signal the change has grown.  (Sec 33.6.)
- Scaling a page view past about 3x segfaults in the Qt FreeType raster
  engine on the stock template's `line-height:0%` text.  Hide the
  template item first, and only after the page view exists.  (Sec 35.3.)


## 4. What is not measured

Stated plainly so the next session does not mistake an argument for a
result.

- **The retention cost of the pre-task on a real model.**  Section 2.7
  reasons about it from `scanner.FCStd`'s collapsible-geometry figure
  and sizes one generation at 3462 bytes on a toy.  Nobody has taken a
  real assembly, broken references in it, and weighed the file.  That
  measurement decides P5 (neighbourhood pruning) and the ceiling.
- **Whether layer 2 fed from a stored base shape repairs materially more
  than layer 2 fed from `_elementCache`.**  Section 2.3 shows it repairs
  *later* (after a reload), which is the durability claim.  It does not
  show it repairs *better*, because P2 does not change the matching
  algorithm -- only P3 onward does.
- **Everything in section 3 past T1.**  The HLR provenance result is
  read from OCCT source and header signatures, not run.  No projected
  edge has yet been traced back to a source element in this tree.
- **The `?Face3` failure is a constructed one.**  Replacing a feature's
  `Base` with a brand-new object is the bluntest possible way to defeat
  layer 1.  It is a real user action -- delete and rebuild a base
  feature -- but the *frequency* of unrepaired references in ordinary
  editing is not measured, and the two side faces in the same run were
  repaired correctly.  The case for the pre-task rests on
  accountability, not on a claimed failure rate.


## 5. The pre-task, planned (2026-09-05)

Written after reading the storage path end to end and measuring four
things sections 2 and 4 left open.  **Three of section 2's claims do not
survive that reading and are corrected in 5.2.**  The plan that follows
is what is left standing.

### 5.1 Scope: what this builds, and what waits

**In scope.** Make the evidence for an element reference *durable* --
persist the snapshot that `Feature::onBeforeChange` already takes, and
feed the existing layer-2 search from it after a reload.  No new matching
algorithm, no new user interface, no change to what a repair decides.

**Deferred, by user ruling (2026-09-05), until upstream's `PartDesign`
and `Sketcher` are merged**: the repair record (old sec 2.9 P3) and the
repair dialog (P4).  This is the right ordering rather than only a
convenient one.  The candidate-list UI a repair dialog would extend
already exists, in `src/Mod/PartDesign/Gui/Utils.cpp:1727` -- it warns on
a missing element reference and offers `getRelatedElements` candidates in
a tree widget -- and that file is squarely inside the merge.  Building a
second UI on top of it now buys rework.

So the pre-task ships as **storage plus the search that consumes it**,
and stops there.  What it produces is not user-visible; what it produces
is a document that still knows what a broken reference used to point at,
which is the thing the deferred UI cannot be built without.

**The failure it exists to fix, stated exactly.**  Section 2.3 measured
`?Face3` and called it "geometry gone forever".  Reading the gathering
code says why, and it is sharper than that:

    if(!element || !element[0] || Data::hasMissingElement(element))
        continue;                       // PartFeature.cpp:1273

A reference that is *already* broken is skipped when the cache is seeded.
So the ladder has no second chance: the snapshot is only ever taken while
the reference is healthy, it lives in memory, and if the document is
saved between the break and the next edit, the last healthy snapshot is
gone.  Layer 2 does not fail because it is weak.  It fails because its
evidence was never written down.

### 5.2 Three corrections from writing the plan

**Correction 1: a content hash names geometry, not names.**  Section 2.4
said "the element map comes with it".  It does not.
`PropertyPartShape::Save` (`PropertyTopoShape.cpp:1060`) writes the
geometry as the blob referenced by `hash=`, and then writes the element
map *separately* -- `_Shape.Save(writer)`, which lands as the
`<ElementMap2>` block inline in `Document.xml` or as a `.Map` file -- and
the string hasher separately again, as a `.Table`.  Three artifacts, one
of which is content-addressed.  Storing a hash and nothing else buys
geometry with no names on it.

Measured on the six-cut plate (`probe_prune.py`), deflated:

| artifact | deflate |
| --- | --- |
| one whole base shape (`Cut5`, 12 faces) | 1613 |
| all six element maps in `Document.xml` | 1240 |

so the map is the same order of cost as the geometry, not a rounding
error on it.  This is what decides 5.3: the stored thing has to be a
*shape property*, which writes all three, and not a bare hash.

**Correction 2: hash plus two more attributes, if the hash route were
taken anyway.**  A blob is not a self-contained shape.  The top-level
location is stripped out of the file and written as `loc=` on `<Part/>`
(`docs/SharedShapeStorage.md` sec 12.2), and a shape that borrows a
congruent instance's file carries `motion=` as well
(`PropertyTopoShape.cpp:1088`).  Only the borrowing *plan* is recoverable
from the file itself -- `serveFromBlob` reads it back out of the parsed
content on purpose.  So "hash -> shape" is not a function; it is
`(hash, loc, motion) -> shape`.

**Correction 3: neighbourhood pruning is dead.**  Section 2.7 left open
whether to store the whole base shape or prune it to the referenced
element plus its adjacent faces, and guessed pruning would save "an order
of magnitude".  Measured, on the same shape, deflated:

| stored | deflate | share |
| --- | --- | --- |
| whole base shape (12 faces) | 1613 | 100% |
| neighbourhood: picked face + the 4 sharing an edge | 1286 | **80%** |
| the picked face alone | 432 | 27% |

Pruning to a neighbourhood saves **20%**, not an order of magnitude.
BRep size is dominated by surfaces and curves, and on any part small
enough to care about, the neighbourhood of a face *is* most of the part.
It is the worst of both options: it keeps most of the cost, and because
the pruned shape is content nothing else in the file holds, it forfeits
the free-when-healthy sharing that the whole shape gets.

**Drop P5.**  The real choice is binary -- the whole shape (free when
healthy, shareable, carries context) or the bare sub-shape (27%, never
free, no context).  The whole shape wins on the common path, which is the
one that runs every save.

### 5.3 Where the shape goes: the layering question, answered

The question is real: `App::PropertyLinkSub` and its three siblings live
in `src/App/`, which cannot see `Part::TopoShape`.

**An App-space geometry property is possible.**  This is worth stating
because it is the option the question implies, and it is not blocked by
layering:

- `App::PropertyComplexGeoData` (`src/App/PropertyGeo.h:611`) is
  App-space and abstract over `Data::ComplexGeoData`, which is itself an
  App-space `Base::Persistence`.  App can `Save` and `Restore` one
  polymorphically without naming the concrete type.
- `Part::PropertyPartShape` is registered --
  `TYPESYSTEM_SOURCE(Part::PropertyPartShape, App::PropertyComplexGeoData)`
  (`PropertyTopoShape.cpp:83`) -- so
  `Base::Type::fromName("Part::PropertyPartShape").createInstance()`
  builds one from App with no link against Part.  That is not a
  speculative trick: it is exactly what
  `DynamicProperty::_addDynamicProperty` already does
  (`DynamicProperty.cpp:184`, `198`) for every dynamic property in every
  document.
- The blob layer is entirely App-space and content-opaque: a
  `FileBlobHandle` is hash, path and size (`FileBlobManager.h:112`), so
  App can *retain* content it cannot parse, and holding the handle is
  itself the retention -- a blob is deleted only when the last handle
  goes away.

So the reason not to put it on the link property is **cost, not
layering**, and the cost is measured.

**The unit is the feature, not the reference.**  Measured on the six-cut
plate (`probe_gen2.py`), against a 17187-byte file:

| retention policy | added, deflated | file growth |
| --- | --- | --- |
| one generation for the one referenced feature | 1572 | **+9.1%** |
| one generation for every feature | 8422 | **+48.9%** |

A base shape is one shape per *feature* however many references point
into it, and the existing `_elementCache` is already keyed that way -- a
map on the feature, from element name to sub-shape.  Storing it on the
link property instead multiplies a 9.1% item by the reference count, and
makes it worse than that: `PropertyPartShape::Copy()`
(`PropertyTopoShape.cpp:984`) copies `_Shape` and `_Ver` and **not**
`_blob`, so every per-reference copy re-serializes and none of them share
the free hash.

**Decision: the snapshot is persisted on the referenced feature, in Part
space, as a shape property.**  Concretely, `Part::Feature` gains

    PropertyPartShape  _SavedShape;      // hidden, the previous generation
    PropertyStringList _SavedElements;   // the element names it was taken for

This is the persistent form of `_elementCache`, put where `TopoShape` is
in scope and where the cache already lives.

**And App needs almost nothing**, because the seam is already there.
`GeoFeature::searchElementCache` is declared virtual in App
(`src/App/GeoFeature.h:176`), returns nothing in the base
(`GeoFeature.cpp:272`), and is implemented in `Part::Feature`.  The one
consumer, `PropertyLinkBase::_updateElementReference`
(`PropertyLinks.cpp:382`), calls it through that virtual and does not
know or care where the evidence came from.  Persist the cache behind the
virtual and every App-side caller gets the durable version with no App
change at all.

The single App-side addition is a **trigger**, not a store -- see step
S3.

### 5.4 What is stored, and when it is not

`_SavedShape` is the *previous* generation, and the invariant is what
keeps the file from growing:

> `_SavedShape` is empty whenever every recorded reference into this
> feature resolves.  It holds geometry only while at least one is
> missing.

Which gives three cases:

| state | what is written |
| --- | --- |
| all references healthy | nothing -- the property is empty |
| a reference just broke | the previous shape; its geometry is a blob the file **already holds** if the feature has not been saved since, otherwise one new blob |
| broken and still broken | the same previous shape, unchanged, so the blob is skipped by the existing content hash and costs no write |

The healthy case is the one that runs on every save of every document
that is working correctly, and it costs nothing.  The 9.1% is what a
document pays *per feature that currently has a broken reference*, which
is the case the feature exists for.

**No ceiling is needed after all.**  Section 2.7 asked for a per-document
cap.  With one generation per feature and retention conditional on an
unresolved reference, the worst case is bounded by the number of features
that simultaneously have broken references -- and a document in that
state has a bigger problem than its size.  A preference to disable the
whole thing is still worth having; a cap is not.

### 5.5 Build steps

**S1. Persist the snapshot.**  Add `_SavedShape` and `_SavedElements` to
`Part::Feature`, both `Prop_Hidden`.  Populate them at the end of the
existing gathering block in `Feature::onBeforeChange`
(`PartFeature.cpp:1226`) -- the snapshot is already taken there, this
only writes it down.  Clear both whenever every entry in
`_SavedElements` resolves against the current `Shape`.

No behaviour change: nothing reads them yet.  This step is measurable on
its own -- a healthy document must save byte-identically to what it saves
today.

**S2. Feed layer 2 from it.**  `Feature::searchElementCache`
(`PartFeature.cpp:1354`) currently returns `none` when `_elementCache`
has no entry for the element.  Make that the point where it falls back to
`_SavedShape`: take the sub-shape named by the element out of the saved
shape, and run the same `searchSubShape` against the live one.

The matching is unchanged.  What changes is that it now also works in
every situation 2.2 listed as opportunistic -- after a reload, inside a
transaction, during an undo, and for a reference whose break happened in
a session that has since ended.

**S3. Fire the search after a restore.**  Storage is inert without this,
and it is the one App-side change.  `GeoFeature::onDocumentRestored`
(`GeoFeature.cpp:265`) already recomputes `_ElementMapVersion`; add: if
this feature has a saved snapshot and any reference into it is missing,
call `PropertyLinkBase::updateElementReferences(this, false)`.

That lands in `_updateElementReference` with `feature == geo` and
`missing == true`, which is exactly the gate the existing recovery
already waits behind (`PropertyLinks.cpp:373`).  Nothing new is invented;
a path that could never fire for a document reopened broken now can.

**S4. A Python accessor, and nothing more.**  Expose the saved element
names and whether a snapshot is held, so the deferred UI has something to
build on and so the gates below can be written.  No dialog, no report
view, no automatic repoint that does not already happen.

Deferred to the `PartDesign` / `Sketcher` merge: the repair record and
the two-picture dialog (old sec 2.9 P3 and P4).

### 5.6 Traps

- **A second `PropertyPartShape` on `Part::Feature` is visited by
  everything that walks shapes.**  The blob owner table, the render
  cache, the store, the property editor and every `getPropertyOfGeometry`
  consumer.  `getPropertyOfGeometry()` returns `&Shape` and
  `onBeforeChange` tests `prop == &Shape`, so the geometry paths are
  safe, but this wants checking against the borrowing and dedup passes of
  `docs/SharedShapeStorage.md` sec 12.12 to 12.14 before it is called
  done -- a saved generation must not become a *source* other shapes
  borrow from, or dropping it later invalidates them.
- **`Copy()` drops the blob.**  `PropertyPartShape::Copy()` carries
  `_Shape` and `_Ver` only.  Assigning the saved shape by copying the
  live property therefore re-serializes rather than sharing the file.
  Take the handle deliberately (`getBlob()`, `PropertyTopoShape.h:148`)
  or accept the extra write and say so.
- **`onBeforeChange` is skipped while restoring and inside a
  transaction.**  Those gates are why the cache is missing exactly when
  it is most wanted, and S1 must not simply inherit them -- but neither
  may it write a snapshot *during* a restore, which would overwrite the
  one being restored.  The condition is "the shape is changing for a
  reason other than being loaded", and it is not the same condition the
  existing block uses.
- **An already-broken reference is skipped by the gathering loop**
  (`PartFeature.cpp:1273`).  That is correct and must stay: the snapshot
  worth keeping is the last *healthy* one, and re-seeding from a broken
  reference would overwrite it with nothing.
- **Do not let S3 turn a restore into a recompute.**  The recovery path
  calls `aboutToSetValue` / `hasSetValue` on link properties.  Firing it
  from `onDocumentRestored` must leave the document not-touched, the way
  `serveFromBlob` takes care to (`PropertyTopoShape.cpp:657`,
  `purgeTouched`).

### 5.7 Gates

Extend `src/Mod/Test/ShapeStorage.py`, run with
`FreeCADCmd -t ShapeStorage`:

1. A document with only healthy references saves byte-identically to a
   build without the feature.
2. Break a reference, save, reopen in a second process: the saved shape
   is present and `_SavedElements` names the element that broke.
3. The same document, reopened: the reference is recovered where the
   geometry still exists -- the `probe_fix3.py` matrix, moved into the
   suite and run across a save boundary rather than in one session.
4. `Face3` of that matrix -- the case that legitimately cannot be
   recovered -- still reports missing, and does **not** get silently
   repointed at a plausible neighbour.
5. Repair the reference by hand, save: the saved shape is dropped and the
   file returns to its healthy size.
6. Undo across the break: the snapshot is not clobbered by the
   transaction the existing gate excludes.

Item 4 is the one that matters most.  The whole argument of section 2.3
is that a plausible wrong answer is worse than a visible missing one, and
a change that makes recovery reach further is exactly the kind that
regresses it.

### 5.8 What this still does not measure

- **Whether persisted layer 2 repairs materially more than live layer
  2.**  It repairs *later*, which is the whole claim and is what gate 3
  tests.  It does not repair *better*: S2 changes when the search runs,
  not how it matches.
- **The cost on a real assembly.**  9.1% is one feature in a 17 KB
  synthetic file.  `docs/SharedShapeStorage.md` sec 11.4's
  `scanner.FCStd` is the model to weigh this against, and nobody has.
- **How often references actually break in ordinary editing.**  Still
  unmeasured, still the honest limit on the case for this work, and still
  the reason it is scoped as accountability rather than as a fix.


## 6. Where to resume (written 2026-09-05, end of session)

Nothing in this document is built.  Sections 1 to 4 are the survey,
section 5 is the plan for the pre-task, and this section is what a next
session needs that neither of them says.

### 6.1 State of play

| | state |
| --- | --- |
| pre-task (sec 5) | planned, decided, **not started** |
| main task, TechDraw (sec 3) | surveyed, sequenced, **not started** |
| measurements | done; all numbers in secs 2.3, 2.5, 5.2, 5.3 are real |
| probe scripts | session scratchpad only, **will be gone** -- recipes in 6.3 |

**Settled, do not relitigate:**

- The snapshot is persisted **on the referenced feature in Part space**,
  as a shape property, not on the App-space link property (5.3).  An
  App-space property was checked and is possible; the reason against it
  is the measured per-reference cost, not layering.
- The stored unit is **the whole base shape**, not a neighbourhood and
  not the bare sub-shape (5.2, correction 3).
- Retention is **conditional on an unresolved reference**; the healthy
  case writes nothing (5.4).  No per-document ceiling.
- **No user-facing repair UI** until upstream's `PartDesign` and
  `Sketcher` are merged (5.1).

### 6.2 Open decisions the next session has to make

These are genuinely open.  Each carries a recommendation, and the
recommendation is not a decision.

**D1. What `_SavedElements` stores -- indexed names, mapped names, or
both.**  This is the one that can quietly defeat the whole feature.  If
the saved element names are *indexed* (`Face3`), they are positional in
the saved shape exactly as they were in the live one, and a saved
snapshot whose map is not consulted has bought nothing over storing the
sub-shape.  The gathering loop keys `_elementCache` on whatever
`Data::findElementName(sub)` returns for each link, which is whichever
form the link holds.

*Recommendation:* store the pair, the way `ShadowSub` already does --
mapped name first, indexed second -- so a lookup can go through the
saved shape's element map and fall back to position only when the map is
absent.  It also makes the saved property self-describing when read by
hand.

**D2. The Sketcher prefix.**  `Part::Feature` supports more than one
geometry property through `registerElementCache(prefix, prop)`, and
`SketchObject` uses it: `registerElementCache(internalPrefix(),
&InternalShape)` (`src/Mod/Sketcher/App/SketchObject.cpp:229`).  A single
`_SavedShape` covers `Shape` only, and references into `InternalShape`
would get nothing.

*Recommendation:* scope S1 to the primary `Shape` and say so in the
commit.  `Sketcher` is one of the two modules in the coming merge, so a
prefix-aware version built now is a version built against code that is
about to move.  Revisit it as part of that merge.

**D3. Where the persistence decision is taken: `onBeforeChange` or
`beforeSave`.**  Trap 3 of 5.6 is that `onBeforeChange` is gated off
during restore and during a transaction, which is exactly when the cache
is most wanted -- but writing a snapshot *during* a restore would
overwrite the one being restored.

*Recommendation:* leave the gathering in `onBeforeChange` completely
unchanged, and take the persistence decision at `beforeSave`: if the
in-memory cache holds a generation, write it; if it does not and
`_SavedShape` already holds one whose references are still missing, keep
what is there.  That makes S1 the safest possible change -- it adds a
writer and touches no existing condition.

*The cost of that recommendation, stated:* a break that happens entirely
inside a transaction is never seeded into the cache in the first place,
so no amount of save-time persistence recovers it.  Undo/redo-mediated
breaks stay unprotected.  Accepting that is the price of not touching
the gathering gates in the first commit; widening them is a separate,
riskier change and should be its own step with its own gate.

**D4. Other `GeoFeature` kinds.**  Mesh and Fem carry element references
too and would get nothing from a `Part::Feature` property.

*Recommendation:* leave them.  `searchElementCache` is virtual on
`App::GeoFeature`, so each kind can implement its own storage later
without any of this changing.

**D5. Whether the saved shape's hasher and map version need handling of
their own.**  `PropertyPartShape::Save` registers its hasher through
`owner->getDocument()->addStringHasher(...)` and writes an
`ElementMap="<version>"` attribute.  A second shape property on the same
object goes through the same path with a *different* generation's map.

*Recommendation:* verify before building, not after.  The specific
question is whether two shape properties on one object can carry element
maps of different vintages without `GeoFeature::updateElementReference`'s
version comparison (`GeoFeature.cpp:243`) reading the wrong one --
`getElementMapVersion(prop)` takes the property, so it probably behaves,
but "probably" is not what this should rest on.

### 6.3 Reproducing the measurements

The probe scripts are in the session scratchpad and are not committed,
per the `DrawBrokenView` convention.  The recipes are short enough to
restate, and every number in this document comes from one of these three
models under
`.conda\run.cmd build\win-relwithdebinfo-801\bin\FreeCADCmd.exe <script>`:

**Model A -- the reference-recovery matrix** (secs 2.3, 2.6; `probe_ref3.py`,
`probe_fix3.py`).  `Part::Box` 20x20x20; `Part::Cylinder` r=3 h=40 at
(10, 10, -10); `Part::Cut` of the two; a `Part::Plane` with
`AttachmentSupport = [(cut, ("FaceN",))]` and `MapMode = "FlatFace"`.
Break it by assigning `cut.Base` a **brand new** `Part::Box` of a
different height -- a new object is what defeats layer 1, changing the
existing box is not.  Read the outcome from
`pl.AttachmentSupport[0][1]`.  Save with
`doc.SaveSchemaVersion = 5` and read `Document.xml` out of the zip for
the `<Link .../>` and `<Part .../>` serialization.

**Model B -- the retention cost** (sec 5.3; `probe_gen2.py`).  A
120x80x10 `Part::Box` plate, then six `Part::Cylinder` r=4 h=30 at
(15 + 18i, 40, -10), each consumed by a `Part::Cut` chained onto the
previous.  Attach a `Part::Plane` to `Face1` of the last cut.  Save at
schema 5, then set `plate.Height = 14` and save again.  Compare
`compress_size` of `blobs/*.brp` across the two saves, and the count of
distinct `hash="..."` in `Document.xml`.

*Do not use a `Part::Fillet` in this model.*  The first attempt did, with
`Edges` built from `range(len(shape.Edges))`, and it failed with "There
are no suitable edges for chamfer or fillet" -- leaving a null shape and
a 154-byte blob that looked like a real measurement.

**Model C -- pruning and map cost** (sec 5.2, correction 3;
`probe_prune.py`).  Model B without the plane.  Export three shapes to
BRep and deflate each with `zlib.compress(data, 6)`: the whole shape;
the picked face; and a `Part::Compound` of the picked face plus every
face sharing an edge with it (compare by `Edge.hashCode()`).  For the map
cost, regex `<ElementMap2 count="\d+">(.*?)</ElementMap2>` out of
`Document.xml` and deflate the concatenated bodies.

### 6.4 First action next session

Build **S1** (5.5) under decisions D1 and D3 as recommended: add
`_SavedShape` and `_SavedElements` to `Part::Feature`, populate them at
`beforeSave` from the existing untouched `_elementCache`, clear them when
every saved element resolves against the current `Shape`.

The gate to write first is **gate 1** of 5.7 -- a healthy document saves
byte-identically to what it saves today.  It is the cheapest thing to get
wrong and the cheapest to check, and until it passes there is no point
running the rest.

Then D5's verification, before S2 touches anything.
