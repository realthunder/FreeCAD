# Corner Blending -- setback vertex blends for fillets

Status: design only. Nothing implemented. The PartDesign feature waits for the
upstream PartDesign port to merge; the OCCT side can start before that.

Request: realthunder/FreeCAD_assembly3#894, "[FR] Blend Corner feature"
(2021-11). Related: #917 (variable radius; the fork's fillet has per-edge
`Segments` since), #677 (tangent-constrained surface between curves).

## 1. What is asked

Where several filleted edges meet at a vertex, today's fillet ends each
stripe where it runs into the others and fills the small hole left between
them. The issue's pictures (from another CAD's "Blend Corner") show the
alternative: each fillet is **set back** along its edge -- cut off a chosen
distance before the vertex -- and the much larger opening is filled with
one smooth patch, tangent to the fillets it meets and to the faces between
them. The result is a softer, rounder corner whose size the user controls
per edge, independently of the radii.

The issue thread adds:

- One commenter asked for a corner *type* drop-down (chamfer / miter /
  blend), as in some CAD packages' chamfer dialogs.
- realthunder (2021-11) suggested OCCT's multi-radius fillet might cover part
  of it. The reporter replied that it does not: a setback corner works for
  any number of edges and on curved faces, and it is not a radius change.
  The fork has since gained per-edge radius segments (`PropertyFilletSegments`);
  they stay separate from this.
- The reporter's manual recipe is blend curve + isocurve + Filling: the same
  construction as section 3 below, done by hand.
- realthunder (2022-02): both this and multi-radius fillet want on-screen
  push/pull handles, which FreeCAD lacked then. That is section 6.

Terms used below, at a corner vertex V:

- `e_1 .. e_n`: the edges at V in cyclic order around it; `F_i` is the face
  between `e_i` and `e_(i+1)`.
- A **stripe** is the fillet surface along a filleted `e_i`. Its end
  **section** is the cross-section curve where it stops. The section's two
  ends are its **contact points** on the two faces of `e_i`.
- `d_i` is the **setback** of `e_i`: the distance from V, measured along the
  edge (the spine), at which its stripe stops. For an edge that is not
  filleted (a **sharp** edge), the corner patch cuts the edge itself at that
  distance.
- A **face curve** `B_i` is a curve on `F_i` joining the end at `e_i` to the
  end at `e_(i+1)`: a contact point, or the cut point of a sharp edge.

The corner patch is bounded by the n sections (or the cut points of sharp
edges) and the n face curves, alternating -- a 2n-sided region, or fewer
where sharp edges give points instead of sections. It is G1 to each stripe
across its section and G1 to each face across its face curve.

## 2. What OCCT does at a vertex today

All in `TKFillet/ChFi3d`. `ChFi3d_Builder::PerformFilletOnVertex` picks a
routine by the number of stripes and edges at the vertex:

| Routine | When | How the corner is filled |
|---|---|---|
| `PerformOneCorner` (`ChFi3d_Builder_C1.cxx`) | one stripe ends at V | the stripe is cut by the face it runs into; no patch |
| `PerformTwoCorner` (`ChFi3d_FilBuilder_C2.cxx`) | two stripes | intersection, a torus (`ChFiKPart`), or a Coons fill along a pivot edge |
| `PerformThreeCorner` (`ChFi3d_FilBuilder_C3.cxx`) | three stripes, three edges | a sphere when it fits (`ChFiKPart_ComputeData::ComputeCorner`), else `GeomFill_ConstrainedFilling` (3-sided) |
| `PerformMoreThreeCorner` (`ChFi3d_Builder_CnCrn.cxx`) | more than three edges, or the others giving up | `GeomPlate` N-sided G1 patch; boundaries are the stripes' end sections and face curves between them |

`PerformMoreThreeCorner` is already the right shape for a setback corner:
its boundaries are exactly the sections plus face curves of section 1, with
setback zero. What it does not have is the setback, and its face curves are
made the fragile way. It builds a 3D Hermite curve between stripe ends and
projects it onto the faces (`CurveHermite`). Or it makes a batten
(`CalculBatten`), or a straight line in UV. The 2026-10 fillet work on this
box fixed four separate failures in exactly that code:

- a projection stored backwards (`3fa9420429`);
- pieces of a curve over several faces in the wrong order (`f1d8509ff6`);
- projected pieces that do not run end to end (`68c5d4efd0`);
- a `GeomPlate` G1 patch folding to keep tangency, with the G0 fallback
  `2b6b625bcb`.

Over 400 of 495 G1 plates in that sweep missed their boundaries by more
than 1e-3. See `~/works/sw/occt/tests/fork/fillet/README.md`. A setback
corner makes the patch larger, which makes all of this matter more.

No public API sets anything per vertex. `BRepFilletAPI_MakeFillet` takes
radii per edge (constant, two-value, `Law_Function`, or (u, r) pairs) and a
fillet shape; `SetRadius(r, IC, V)` sets the radius *at* a vertex, not a
corner.

## 3. OCCT design

### 3.1 API (ABI-safe)

`BRepFilletAPI_MakeFillet` holds its `ChFi3d_FilBuilder` by value, so FreeCAD
compiled against the fork has the builder's layout baked in. The plate G0
threshold went in as a static for that reason (`2b6b625bcb`, "without
breaking ABI"). Setbacks are per contour end, so they go where layout is
private to TKFillet: `ChFiDS_FilSpine`, allocated only inside the builder
and reached through a handle.

New non-virtual exported methods. Adding these does not move any layout:

```
// BRepFilletAPI_MakeFillet
void   SetSetback(const TopoDS_Vertex& V, const TopoDS_Edge& E, double D);
void   SetSetback(const TopoDS_Vertex& V, double D);   // every edge at V
double Setback(const TopoDS_Vertex& V, const TopoDS_Edge& E) const;  // 0 = none
```

A setback on a filleted edge is stored on its contour's spine, at the end
that is V: `ChFiDS_FilSpine::SetSetback(bool isFirst, double)`. A setback
on a sharp edge has no spine; it lives in a vertex-keyed map. That map
would be a new member, so it cannot go on the builder. It goes on the spine
of any stripe that ends at V, which is enough because a corner with no
stripe at all is not a corner. A vertex with any setback set is a
**setback corner**, and every edge at it gets a setback (default below).

**Default setback.** Today's corner, the zero-setback one, already has an
effective setback on each edge: the distance from V at which its stripe
ends. Call it `d0_i`. A setback below `d0_i` is meaningless. So
`d_i = max(requested, d0_i)`, and `SetSetback(V, D)` with D less than every
`d0_i` gives today's corner shape built the new way. That is the useful
regression test of 3.4.

### 3.2 Algorithm

1. **Walk as today.** The stripes are computed to V as they are now. That
   gives `d0_i` and the contact curves near V, which the face curves need
   for their tangents.

2. **Cut each stripe back** to spine abscissa `w_V -+ d_i`. Cut the last
   SurfData there, or drop it and cut the one before. The cutting exists in
   pieces already: `ChFi3d_Builder::Trunc` for the analytic kinds, and the
   corner routines' own surfdata trimming (`RemoveSD` in CnCrn). It has to
   become one helper that leaves the stripe ending in a proper section: the
   isoparametric curve of the fillet surface, its two pcurves on the faces,
   and two `ChFiDS_CommonPoint`s that are not on an arc. A setback that runs
   past the stripe's first surfdata on that edge, or past the edge's other
   end, is an error (3.3).

3. **Route to one routine.** Every setback corner goes to a new
   `PerformSetbackCorner(Index)`, whatever its count of stripes and edges.
   The case table of section 2 stays for ordinary corners. The new routine
   is the N-sided filler with clean inputs. Its core should be lifted out
   of `PerformMoreThreeCorner` (the plate construction, orientation, and DS
   insertion: `PlateOrientation`, the `SolidSurfaceInterference`, the curve
   interferences). It should not be a fifth copy of that code.

4. **Face curves in UV, not projected.** On each `F_i`, build `B_i` directly
   as a 2D curve in the face's parameter space. It runs from the end on
   `e_i` (the stripe's contact point, or a sharp edge's cut point) to the
   end on `e_(i+1)`. Its end tangents are those of the stripes' contact
   curves at those points, mapped to UV through the surface's first
   derivatives, so the patch can be G1 to the stripes at the corners of
   the region. Use a 2D cubic Hermite with the tangent magnitudes scaled to
   the chord, as `CurveHermite` does in 3D. The 3D curve is the curve on
   the surface. Nothing gets projected, which removes the whole class of
   failures listed in section 2.
   - A periodic face (cylinder, sphere) unwraps: take both ends' UV from the
     pcurves of the edges and stripes at V, which already sit on one side
     of the seam, and shift by a period where they straddle it.
   - Check `B_i` against the face with `BRepTopAdaptor_FClass2d` at a few
     samples. A curve that leaves `F_i` means the setback does not fit on
     that face, which is an error (3.3). A face curve that would have to
     cross onto a neighbouring face, the `moresurf` case, is out of scope
     for the first version and is an error too.

5. **Fill.** `GeomPlate_BuildPlateSurface` with one `GeomPlate_CurveConstraint`
   per boundary:
   - each section: order 1, against the stripe surface;
   - each face curve: order 1, against `F_i`;
   - a sharp edge's cut point is a corner of the region, with a
     `GeomPlate_PointConstraint` at it.

   `GeomPlate_MakeApprox`, then the fork's G0 fallback (`PlateG0Fallback`),
   then the boundary-orientation check of `3fa9420429`. For n = 3, all
   filleted, on planes, the plate should come out close to the current
   `GeomFill_ConstrainedFilling` result at setback `d0`. That comparison is
   a test.

6. **DS.** Insert the plate face, its edges shared with the stripes (the
   sections) and with the faces (the face curves: face interferences that
   trim each `F_i`), and the sharp edges trimmed at their cut points. This
   is all done today in CnCrn for the zero-setback plate, and should come
   from the factored code of step 3.

### 3.3 Errors

A setback corner that cannot be built must fail with a status, never leave
an invalid shape behind. The 2026-10 work turned 67 invalid results into
exceptions for this reason. Add `ChFiDS_ErrorStatus` values, or reuse
`ChFiDS_Error` with a faulty vertex, for each of:

- the setback exceeds the edge (it would pass the other end, or meet that
  end's own setback or stripe end);
- the setback runs past the stripe's single surfdata on that edge, when the
  stripe changes faces before V;
- a face curve leaves its face;
- the plate misses a boundary by more than the tolerance, with the G0
  fallback included.

Each one names the vertex, so FreeCAD can point at it.

### 3.4 Tests (in `tests/fork/fillet`, own section)

- A box corner with 3 equal radii, setback equal on all three edges, at
  `d0`, 2x `d0` and 4x `d0`: valid, one closed shell, and volume decreasing
  monotonically with setback.
- The same with unequal setbacks, and with unequal radii.
- At `d0`: the volume within tolerance of today's corner (the sphere, or
  `ConstrainedFilling`).
- A 4-edge vertex (a pyramid apex) and a 5-edge one.
- Mixed: two filleted edges and one sharp edge, so the patch meets the
  sharp edge's cut point.
- Curved faces: a cylinder boss on a plate (the edge circle plus the
  seam-free side), and a corner on a cylinder's seam.
- Every error of 3.3, each with its status.
- Pictures for the doc, same tooling as `models/Fillet.md`.

## 4. FreeCAD side

### 4.1 Part

- `TopoShape::makeElementFillet` (and the older `makEFillet` overloads that
  carry `FilletSegments`) takes one more argument: corner settings, a list
  of `{vertex, default setback, map edge -> setback}`. Element names are
  resolved to vertices and edges of the base shape the same way the
  segments' edge names are. The settings are passed through to
  `BRepFilletAPI_MakeFillet::SetSetback`.
- History: the corner patch face is `Generated` from V. That is the mapped
  element name `Face;:G(Vertex<n>)` or similar, through the existing
  `makeElementShape` path, so a later feature can reference the corner face
  stably.
- The fork's OCCT is the only one with `SetSetback`. Like the plate
  fallback (`AppPartPy setOCCTPlateG0Fallback`, dlsym), resolve it at run
  time. On another OCCT, a setback corner is an error: "corner setback
  needs the fork's OCCT". It must not silently build a different shape.

### 4.2 PartDesign (after the upstream PD port merges)

- `PartDesign::Fillet` gets a `Corners` property next to `Segments`: a new
  `Part::PropertyFilletCorners` in `PropertyDressUp.h`, modelled on
  `PropertyFilletSegments`.
  - Keyed by vertex sub-name (`"Vertex12"`). The value is
    `{setback, {edge sub-name: setback}}`.
  - `connectLinkProperty(Base)`, so the names follow topology changes as
    the segments' names do.
  - Python get/set and path values (`Corners.Vertex12.Setback`) so
    expressions can drive it.
- A vertex is only meaningful if at least one of its edges is in the
  fillet. The feature keeps entries for vertices that drop out (as
  `Segments` does), but ignores them, with a warning.
- Old files have no `Corners` and restore as today. A new file opened by a
  FreeCAD without the property loses the corners and keeps the plain
  fillet. That is the usual PD forward-compat behaviour; no
  `handleChangedPropertyName` needed.
- Chamfer: out of scope. A chamfer corner (`ChFi3d_ChBuilder`) has its own
  corner code. If wanted later, the same API and property apply.

### 4.3 Part workbench

`Part::Fillet` (`PropertyFilletEdges`) can get the same setting later. The
first version is PartDesign only, plus the Python `TopoShape` API, which is
enough to test from scripts and the suite.

## 5. Corner types beyond setback

The drop-down idea from the thread (chamfer / miter / blend corner types)
is a different feature. "Miter" is two stripes extended and cut by a plane
through V. "Chamfer" closes the corner with a flat patch. Both would fit
the same per-vertex property as `Type`, with `Setback` as the first type
beyond `Default`. Do not build them in the first version, but keep the
property's value open for a type field.

## 6. GUI

realthunder's 2022 comment holds: a corner with n setbacks is unusable
without direct manipulation. The task panel shows:

- a corner list (pick a vertex in the 3D view; it is added with the default
  setback);
- per-edge setback fields under each corner;
- one drag handle per edge at the setback point, moving along the edge.
  Same interaction as the segment handles on a variable-radius fillet in
  `TaskFilletParameters` (reuse that code); the preview recomputes on
  release.

The handle position is the stripe's section point on the edge, so it needs
nothing from OCCT beyond the spine abscissa, which `Abscissa` /
`RelativeAbscissa` already give.

## 7. Phases

1. **OCCT prototype.** `PerformSetbackCorner` for all-filleted corners on
   planar faces; the API; a C++ driver; the box-corner tests of 3.4.
   Compare against today's corner at `d0`.
2. **OCCT general.** Sharp edges at the corner, curved and periodic faces,
   the 3.3 errors, the full 3.4 set; the every-edge and vertex sweeps
   unchanged for corners without setbacks. That last point holds by
   construction, because nothing routes to the new code without a
   setback; the sweeps prove it.
3. **Part API.** `makeElementFillet` corner settings, dlsym guard, Python.
4. **PartDesign property and feature.** After the PD port merges.
5. **Task panel and drag handles.**

Phases 1 and 2 are a few days to a couple of weeks of ChFi3d work. The risk
is in step 2 of 3.2 (cutting a stripe cleanly at an arbitrary abscissa) and
in the plate quality of step 5. Factoring CnCrn's plate-to-DS code (step 3)
is the part most likely to grow. Do it as its own commit before any setback
code, and gate it on the sweeps not moving.

## 8. Open questions

- **Setback on a sharp edge with no setback given.** Use the largest
  neighbouring setback, or the mean of the two neighbours? CAD packages
  differ. Start with the largest, which gives a symmetric look on a box
  corner.
- **Setback measured along the edge or along the spine?** They are the same
  except on a contour that turns at V tangentially. Measure along the spine,
  since that is what the stripe walk uses.
- **Interaction with radius segments.** A segment whose range overlaps the
  setback region is cut by it; the radius law still applies up to the
  section. Is that what users expect? Probably yes; check once a prototype
  exists.
- **G1 quality.** If the plate misses its boundaries as often as the
  zero-setback plates do, the corner shows creases where it meets the
  stripes. Measure the G1 error on the 3.4 cases before building any GUI. A
  2n-sided patch with good inputs may do far better than today's plates,
  which are fed projected curves.
