# A primitive's elements, named by what they are

> Asked for 2026-10-07, built 2026-10-09. The user: "those primitive shapes
> (box, cylinder, both Part and PartDesign) that current have no element
> name and only indexed ones. I want to name them", and, to how: "By role.
> No need type." All Part and PartDesign primitives; not an import's shape;
> an older file's primitive when it is next made.

## 1. What was wrong

A primitive is built by the kernel with nothing said of which face is
which, and its shape had no element map. So:

- A reference to a face of a box was a number and nothing more. Where the
  primitive counts its faces another way the number is another face, and
  nothing says so. A cone whose `Radius1` goes to zero has two faces where
  it had three; a cylinder with `Angle` 90 has five.
- Everything made from a primitive is named from those numbers:
  `Face3;:H64,F`, `Face6;:M;CUT;:H64:7,F`. The same shift, one step on.
- The first primitive of a body had no names at all, and a sketch on its
  `Face6` held a bare number.

*What was not wrong, checked because it was said to be:* a box's `Face1`
and a cylinder's are told apart in every derived name by the tag of the
object, `:H762` against `:H-763`. And the sign of that tag is not a
primitive's trouble: a feature's own tool is tagged with its id negated
inside its own step, its finished shape with its id by the step after, and
a pad does exactly the same (`#46;:H-ebc,F` second in a body,
`#5c;:He75,F` seen from the feature after it). A primitive is as a pad is
now, first in a body or later.

## 2. The names

`Part::PrimitiveNames` (`src/Mod/Part/App/PrimitiveNames.{h,cpp}`). A
face is named by its role, an edge by the faces it is between, a vertex by
the faces that meet in it.

| | faces |
| --- | --- |
| box, wedge | `Left` `Right` (-X, +X), `Front` `Rear` (-Y, +Y), `Bottom` `Top` (-Z, +Z), as the views are called |
| cylinder, cone, sphere, ellipsoid | `Lateral`, `Bottom`, `Top`; not all the way round, `Start` (at the angle nought) and `End` |
| prism | `Bottom`, `Top`, `Side1` on, the first from the corner on +X |
| torus | `Lateral`, `Start`, `End`; the tube not whole, `TubeStart` and `TubeEnd` |
| plane | the face `Plane`; its edges `Front` `Rear` `Left` `Right`; its vertices by the two edges, `Front_Left` |
| line, circle, ellipse | the edge by that name; the vertices `Start`, `End` |
| helix, spiral | `Segment1` on; `Start`, `End`, `Joint1` on |
| regular polygon | `Side1` on, `Corner1` on |
| vertex | `Point` |

- An edge: `Front_Top`, its two faces in the order of the alphabet. An edge
  of one face is its seam, `Lateral_Seam`, or where the face closes to a
  point: `Lateral_BottomPole`, `Lateral_TopPole`.
- A vertex: `Bottom_Front_Left_Corner`. On the axis of a cone, a sphere or
  an ellipsoid with no cap at that end it is `BottomPole` or `TopPole` --
  by the end and not by a number, so that a cap put on the other end
  leaves it the pole it was.
- What is still alike is numbered from the second on, in the order of
  height, then of the angle about Z, then of the distance from it:
  an ellipsoid split in halves is `Lateral` and `Lateral2`, a whole
  torus' two seams `Lateral_Seam` and `Lateral_Seam2`.
- A face that fits no role of its kind is `Other`. None is known to.

`Rear` and not `Back`, and -Y for `Front`: FreeCAD's views, not OCCT's box
(whose front is +X). *Mine.* So are the pole names, `Seam`, `Corner`,
`TubeStart`, and the choice that an edge has no mark of its kind while a
vertex has `_Corner`: a mapped name is one key for every kind of element,
and a cylinder's seam end lies in the same two faces as the circle it is
on.

*What it holds through.* Measured on every kind
(`parttests.PrimitiveNamesTest`): a cylinder cut open keeps `Lateral`,
`Bottom`, `Top` and gains `Start`, `End`; pushed askew
(`FirstAngle`, `SecondAngle`) the same; a cone without its bottom keeps
`Lateral` and `Top`; a sphere with a cap keeps the other pole; a wedge
closed to a ridge has the edge `Left_Right`.

*What it does not.* A prism's `Side3` is the third side, and is another
when `Polygon` changes; a helix' `Segment2` likewise. There is no role to
name them by.

## 3. How

The roles are read from the finished shape, in the frame it is built in:
by which way a face faces and where it lies. Not from the builder: Part
and PartDesign have a copy each of sixteen builders that end in a prism, a
revolution, a stretched sphere or a `BRepPrim` solid, and one reading of
the result serves them all.

- *Box, wedge:* flat faces by their outward normal. A wedge's sides lean,
  each about one axis: what faces along X has nothing of Z in it.
- *About the axis:* a face not flat is `Lateral`; flat and facing -Z or
  +Z, `Bottom` or `Top`; flat otherwise, a side, and `Start` where one of
  its edges has its middle on the +X side of the XZ plane. Of a cylinder
  and a prism, which may be pushed askew, only the edge on the floor is
  asked.
- *Torus:* what closes the tube may be flat and face up or down, so it is
  known by where it is -- it has a vertex on the circle the tube is about
  -- and which end by the tube's angle there. That angle is as
  `TopoShape::makeTorus()` lays the tube: from +X, turning toward -Z. The
  torus says its radius and the two angles (`PrimitiveNames::Tube`).

Part: every primitive's `execute()` hands `Shape` the named shape
(`PrimitiveNames::named()`). PartDesign: `FeaturePrimitive::execute()`
names what the feature adds or takes away before it is tagged and made one
with the base. Cost, a box: 64 us against 23 us bare, 26 names.

## 4. What it changes

- **Every name made from a primitive.** `Left;:H961,F` where it was
  `Face1;:H961,F`. See sec 5.
- **A picked face of a box is told by its name**, `;Top.Face6`, as a
  cut's always was. Three browser-serve tests compared with `Face6`.
- **A primitive's painted face is held by name**
  (docs/ShapeAppearanceDesign.md sec 14.3): the looks of a box are merged
  by name, and a link's name for a box's face is saved with the mapped
  name beside it. What was held by number in an older file still is, with
  the name laid over it.
- **The shape's fragment in the log has the names**: 879 bytes for a box
  where it was under 512.

## 5. An older file

*Measured*, on a file the 2025 release wrote: a box, a cylinder, a cut,
and three planes attached to a face of the box and to two of the cut
(`~/.cache/txnlog-link/prim_old_read.py`).

- Recomputed as it is, everything follows. The box gets its names, the
  cut's names change, and each reference into the cut is found again by
  where its face is (`auto change element reference ... ;Face6;:M;CUT;...
  -> ;Top;:M;CUT;...`). A reference to the box itself is a number and
  simply gains its name.
- **With the box changed before that first recompute, a reference into
  the cut is lost**: `Cut.?Face3`, the plane invalid. The name it has is
  made from `Face6`, the cut has no such name any more, and the face is
  not where it was to be found by.

So a primitive read with no names asks for the recompute the fork asks
for whenever a shape's names are of an older making
(`Document::addRecomputeObject()`, in `Part::Primitive::onDocumentRestored()`
and `PartDesign::FeaturePrimitive::onDocumentRestored()`): the document is
flagged, and the GUI says "Some document(s) require recomputation for
migration purpose ... before any modification" and offers it. A file of
the 2025 release was asking already -- its shapes are of OCCT 7 -- and
what is new is that a file this fork wrote before today asks too, where it
has a primitive.

*The user's answer was "at its next recompute, nothing special at load".*
It was given when the names were to be `Face3` itself, which changes no
derived name. With role names the premise is gone, and this is the least
that keeps a reference from being lost; it is still the next recompute
that names them. **To be ruled.**

*Not built, and would close it without a prompt:* on a miss, a name whose
root is a number of an object that now has a name for that element could
be read through that object's names -- `Face6;:M;CUT;:H961:7,F` as
`Top;:M;CUT;:H961:7,F` -- which is exact wherever the primitive counts as
it did, the same assumption the number made. It is in
`PropertyLinkBase::_updateElementReference()`, needs the hashed names read
back, and does not reach a PartDesign tool (a negated tag). Offered, not
started.

## 6. Found on the way

- **A crash on a branch switch** (fixed, its own commit). A box's painted
  faces are held by name now, and what is drawn of them is a list of its
  own in the value the log holds, read from a file after the rest of the
  value. Between the two the object was told of the change, made what is
  drawn from a value half there, and dropped the list
  (`PropertyElementAppearance::setDrawn()`, `pruneHeld()`), which the
  reader then wrote into: SIGSEGV in `App::restoreValue()`. A list that
  waits for its file is not let go of (`Held::awaits()`). There before
  this: any named shape whose drawn list is what is stated could have met
  it. Asked of the session whose meshing crashes were fixed the same
  week: not the same -- those were a worker thread and a drain slice, in
  the Gui.
- **A crash as the process ends** (fixed, its own commit). Three C++
  cases failed with shape freezing on and passed without, and passed run
  by hand. The thread that keeps the log was still writing a shape's
  names while `exit()` ran, and `Data::IndexedName::set()` keeps the kinds
  of element it has met in a table that is a static of the function:
  gone by then. No mapped name had reached that table before -- a name
  made of anything but letters is turned away earlier, and every name was
  `Face1;:H64,F` -- and `Front_Left` is letters. The table is never
  destroyed now, and is written under a lock, the two threads being two.
  *Not looked for:* what else that thread touches after the statics go.
- **A child view provider on a link to a box painted nothing by name**
  (fixed). Seen through a link a shape's names are the linked object's
  with its tag after them, `Front;:H281,F`, and the link's reference has
  the object's own, `Front`. A cut's was found as a name made from the
  one looked for; a name with no tag in it is not. The reference is
  resolved to the element of the object that has it
  (`namedElements()`).
- **Looks read back and written again gave a painted face the object's
  gloss for its own** (fixed). A face held by name states what it was
  given, and the looks are read back whole: a name given what it says
  already states no more than it did
  (`PropertyElementAppearance::setStated()`). A cut's face did this
  before; a box's did not, being held by number, and the paint check has
  the case on a box. *Mine:* `ea["Face3"] = material`, one name given a
  material outright, is still all its own.

## 7. Checks

Python `parttests.PrimitiveNamesTest`, 12, and `PrimitiveNamesBodyTest`, 2: every kind's roles and
that none is unnamed, alike or `Other`; each through the change that
renumbers it; the names in a file and read back, with nothing asked to be
made again; what is made from a primitive named by its roles; a reference
following its face through a cylinder cut open; PartDesign's eight first
in a body, and one after another feature.
`parttests.ElementAppearanceTest`, +1: a box painted on a branch, left
and come back to. The four tests that named a primitive's elements by
number say the roles.
