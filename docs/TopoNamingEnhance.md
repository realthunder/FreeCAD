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

Probe scripts for everything measured here are in the session
scratchpad, following the `DrawBrokenView` convention of not committing
them: `probe_ref.py`, `probe_ref3.py`, `probe_fix3.py`.


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

- **The element map comes with it.**  A stored base shape carries its
  `<ElementMap2>` block, so the old mapped name can be looked up in the
  old map, and layer 3's ancestry walk becomes available across a reload
  rather than only while every ancestor object still exists.
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
