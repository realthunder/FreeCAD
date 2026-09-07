# TechDraw stores what it projected

Status: built, 2026-09-07, on `LinkVibe`.

Until now a TechDraw view kept nothing of what it drew.  The projected
edges, vertices and faces -- and a section's cut faces -- lived in
`GeometryObject`, a transient member, and the only way to get them back
was to make them again.  So every time a drawing was reopened,
`DrawPage::onDocumentRestored()` called `updateAllViews()`, which called
`recomputeFeature()` on every `DrawViewPart` on the page, which ran the
boolean cut (for a section), HLR, and face finding, to arrive at exactly
the picture that had been on the screen when the file was written.

This document is the change that stops that: **the projection is written
into the document, and a restored page draws it instead of rebuilding
it.**

## 1. What was there before, and what was not

- `DrawViewPart::m_geometryObject` -- transient.  Edges, vertices,
  faces, all rebuilt on every recompute.
- `DrawViewSection::m_cutShape`, `m_cutShapeRaw`, `m_cutPieces`,
  `m_preparedShape`, `m_sectionTopoDSFaces`, `m_tdSectionFaces` -- all
  transient, all rebuilt (`docs/TechDrawPortAndSection.md` sec 31.1 says
  so outright: "Transient, like the shapes: they rebuild on
  recompute/restore").
- `DrawViewDimension::SavedGeometry` (`Part::PropertyTopoShapeList`) --
  the one thing a view already stored, and it stores a *dimension's*
  reference geometry, not the projection.
- `DrawViewPart::UnderlayImage` -- the shaded raster underlay is
  persisted (sec 26 of the port doc), which means the *picture* survived
  a reload but the geometry under it did not.

So: no, TechDraw did not store its section geometry, or any other
projected geometry.  It stored a raster of it.

## 2. What is stored now

One property on `DrawViewPart`, inherited by every view that projects --
section, detail, projection-group item, broken view:

    ProjectedGeometry   TechDraw::PropertyProjectedGeometry
                        Prop_Output | Prop_Hidden

`Prop_Output` for the reason `UnderlayImage` has it: the value is
derived state written back by the thing that produced it, and writing it
must not touch the view, or every projection would schedule the next
one.

The property holds

- the **projected edges**, in the order the view numbers them, each as
  the `TopoDS_Edge` it carries plus the attributes `BaseGeom` keeps
  beside it: geometry type, class of edge, HLR visibility, reversed,
  `ref3D`, the source element name and the projected edge's own name
  (`docs/TopoNamingEnhance.md` sec 8.5), cosmetic flag, source and
  source index, cosmetic tag;
- the **projected vertices**, in order: point, visibility, centre-mark
  and reference flags, cosmetic tag, the source names and the vertex's
  own name (sec 8.6.1);
- the **projected faces**, in order: the edges of each bounding wire,
  flattened, with the count per wire, plus the face's source names and
  its own name;
- the **centroid** the projection was made against
  (`DrawViewPart::getOriginalCentroid`), which nothing else stores;
- the **cut faces** of a section (`m_sectionTopoDSFaces`), which is what
  the cut surface is drawn and hatched from.  Empty for every other kind
  of view.

The curves travel as a BRep file of the property's own -- one compound
of three: the edges, the face-wire edges, the cut faces -- because that
is the only form that round trips a projection exactly.
`BaseGeom::baseFactory()` reads an edge back into the same subclass with
the same derived values HLR gave it, which is the same call the
projection itself makes.  Everything an edge does not carry is an XML
record beside it, one per element **in the view's own order**: that is
what keeps `Edge3` the same `Edge3` after a reload, which the whole of
the naming work (T3) depends on.

### 2.1 Where it is captured

Where the geometry is complete, and nowhere else:

- `DrawViewPart::onFacesFinished`, after `postFaceExtractionTasks()` --
  so the capture includes the cosmetic vertexes, cosmetic edges and
  format names `postHlrTasks` added, and the centre lines that had to
  wait for faces;
- `DrawViewPart::onHlrFinished`, in the branch where no face finding
  will run at all (`CoarseView`, or faces switched off).

`DrawViewSection` overrides `captureGeometry()` to put
`m_sectionTopoDSFaces` in beside the projection.

### 2.2 Where it is put back

`DrawViewPart::onDocumentRestored()` builds a `GeometryObject` from the
property and installs it, restores the centroid and the bounding box.
`DrawViewSection` additionally rebuilds `m_tdSectionFaces` from the
stored cut faces with `makeTDSectionFaces` -- a face walk instead of a
boolean.

The restore refuses rather than guesses.  If the stored edge count does
not match the record count, or an edge does not read back through
`baseFactory`, nothing is installed and the view projects the way it
always did.  A stored format newer than this build reads is skipped the
same way.

## 3. Losing the rebuild on restore

`DrawPage::onDocumentRestored()` called `updateAllViews()`
unconditionally (under `canUpdate()`).  It now calls
`updateAllViews(true)`, and the first loop -- the one that recomputes
every `DrawViewPart` -- skips a view when `canReuseStoredGeometry()`:

    m_geometryFromStore && !m_restoredOutOfDate && !isTouched()

The second loop is unchanged: dimensions, balloons, hatches, leader
lines and the rest are still recomputed, because what they derive
(measurement points, hatch line sets) is transient and cheap, and it is
read off the restored geometry.

**`m_restoredOutOfDate` is read in the view's own `onDocumentRestored`,
not in the page's.**  `Document::afterRestore` walks objects in
dependency order and purges the touched flag as it goes, so by the time
the page is reached its views have already lost the "I was written out
of date" mark that the file carried (`Touched="1"`,
`Document.cpp:1918`).  The view reads it first, because the page depends
on its views and is therefore restored after them.

### 3.1 The other view that projected on restore

`DrawProjGroupItem::onDocumentRestored()` did not go through the page at
all: it called `DrawView::onDocumentRestored()` -- skipping
`DrawViewPart`'s -- and then ran its own `execute()` outright, so every
item of every projection group re-projected on restore whatever the page
decided.  It now calls `DrawViewPart::onDocumentRestored()` and returns
when the projection came back with the document; the `execute()` is the
fallback for when it did not.

### 3.2 The switch

`TechDraw/General/StoreProjectedGeometry` (default **on**),
`Preferences::storeProjectedGeometry()`.  Off, a view stores nothing and
a restored page rebuilds exactly as it always did -- which is also how
the before-and-after in section 4 is measured in one build.  A document
written with it on and opened with it off simply reprojects and then
overwrites what it stored.

### 3.3 The dimensions still have to be told

`updateAllViews()` has always had two loops: the part views, which
project, and then everything derived from them -- dimensions, balloons,
hatches, leader lines -- which is recomputed with `overrideKeepUpdated`.
The second loop is now `updateDerivedViews()`, and the restore runs it
even when the page may **not** update, provided some view brought a
projection back.  What those views hold is transient and cheap, and it
is read off the projected geometry, not off the model -- so this is not
the page following the model, it is the restore finishing.  Without it,
a page with `KeepUpdated` off comes back drawn (which is new) with every
dimension reading zero (which would be new and wrong).

## 4. Measured (2026-09-07)

Two probes, both under the GUI harness (`run_cdb.ps1 -StartupScript`,
`docs/TopoNamingEnhance.md` sec 8.0), scratchpad scripts in keeping with
the probe convention.

**It comes back element for element.**  A 40x30x20 box with a 12mm hole
through it, a base view, a section through the hole, and a dimension on
`Edge0`:

    built   view (5 edges, 7 vertices, 2 faces), section (10, 8, 4)
    saved   31754 bytes, closed, reopened
    at the instant open() returned, before any thread could run:
            the view already had its (5, 7, 2)
    reload  the same counts, the same six element names, the same
            vertex and face names, the dimension still 30.000, the
            section still 2 cut faces
    verdict identical = True, for the view and for the section

**And it comes back where nothing is allowed to project.**  The same
document with `KeepUpdated` off, which is the page saying it may not
follow the model: the restore recomputes nothing, so geometry that is
there can only have come out of the file.  Identical again, cut faces
included.  (This is also the case that made
`updateDerivedViews()` necessary -- see 3.3.)

**What it costs, and what it saves.**  A 200x120x20 plate with 84 holes,
a base view (88 edges, 256 vertices, 85 faces) and a section (40, 28,
14), measured twice in one build by flipping the preference of 3.2 --
which makes the second run exactly the old behaviour:

                          stored          not stored
    file                  94736 bytes     76338 bytes   (+18398, +24%)
    open() returns        0.282 s         0.249 s
    page fully drawn      0.001 s         0.542 s

The last row is the point: with the projection stored, the page is drawn
when `openDocument` returns.  Without it, the cut, HLR and face finding
run again after the open and the page fills in half a second later --
for a model that takes 4.7 s to build from scratch.  The file cost is
about 35 compressed bytes per stored element.

**And it draws the same drawing.**  The counts and the names are one
thing; what reaches the paper is another.  The same page, exported with
`TechDrawGui.exportPageAsSvg` straight off the stored projection and
again after being forced to project the whole page: **the same 290313
bytes, the same 705 path elements, the same path data**.  The two files
are not byte-identical -- the vertex dots come out in a different order
within the scene, because a rebuild recreates those items -- and that
raised the obvious question, which was then asked of the geometry
instead of the paper:

    from the store           256 verts, 88 visible edges
    projected again          256 verts, 88 visible edges
    view    same vertex order = True, same edge order = True
    section same vertex order = True
    projection vs projection: same order = True

So the numbering a stored projection comes back with is the numbering a
fresh projection of the same shape produces, element for element; the
SVG difference is the exporter's scene order and nothing else.

## 5. What this does not do

- **The 3D shapes are still transient.**  A section's `m_cutShape`,
  `m_cutPieces` and `m_preparedShape`, and a detail's `m_detailShape`,
  are not stored.  Nothing that *draws* needs them; the first real
  recompute rebuilds them as before.  A detail view on a section draws
  from its own stored projection, so it does not need its parent's cut
  either.
- **Nothing is deduplicated across views.**  Two identical views store
  two copies.
- **The stored projection is never consulted by a recompute.**  It is
  storage for the restore path only; any recompute overwrites it.
