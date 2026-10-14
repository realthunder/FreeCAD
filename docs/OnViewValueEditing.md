# On-view value editing: dress-up handles and typed values in the 3D view

A design proposal, not built. Asked by the reporter on 2026-10-14
(`docs/HandsOnQueue.md` entry 117): rework the dress-up features' editing
manipulator, keeping in mind that this fork's fillet has a default radius
and per-edge radii (one or two, or more along the edge); and, in general,
let a value be typed in the view, without the task panel, through the
editable datum label. A survey of other CAD programs first, then a design.

Written by the note-taking session from a read of the source (nothing run)
and a web search. Where a statement about another program comes from
general knowledge and not from a page found, it says so.

## 1. What is here today

### 1.1 The fillet's data (PartDesign::Fillet)

- `Radius` -- the DEFAULT radius (`FeatureFillet.h` 42; must be > 0).
- `Segments` (`Part::PropertyFilletSegments`, `PropertyDressUp.h` 32 to
  84) -- per edge, a list of `FilletSegment {param 0..1, radius, length}`
  (`TopoShape.h` 2168 to 2189); `length > 0` is an absolute distance from
  the edge's first vertex and overrides `param`. This is a variable-radius
  law: any number of (position, radius) points per edge.
  - An edge with no entry gets `(0, Radius)`: that is "uses the default".
  - A first point with `param > 0` gets `(0, Radius)` put before it; a last
    point with `param < 1` gets `(1, its own radius)` put after it
    (`TopoShapeEx.cpp` 5318 to 5365) -- the start falls back to the default,
    the end does not.
  - `(0, 0)` as the first point suppresses the fillet on that edge.
  - "Two radii" (start and end) are two points, at 0 and 1. There is no
    separate r1/r2 field in PartDesign; `Part::FilletElement` (r1, r2) is
    Part's own fillet, not this one.
  - Expression paths exist per point: `Segments.<Edge>[i].Radius`,
    `.Param`, `.Length` (`TaskFilletParameters.cpp` 553 to 557).
- `Corners` -- setback corners per vertex: a setback for all its fillets,
  per-edge setbacks, and per-face depths (`PropertyDressUp.h` 87 to 193).
- `UseAllEdges`.

### 1.2 The fillet's panel and handles

- One tree: edge rows with columns Radius / Parameter / Length / Setback;
  under an edge, its segment rows; corners as rows with their fillets and
  faces (`TaskFilletParameters.ui` 133 to 154).
- Handles (`setupGizmos`, `TaskFilletParameters.cpp` 215 to 282):
  - TWO radius arrows, both bound to the DEFAULT radius box, on the FIRST
    edge only, one lying in each face beside it (305 to 321). That is the
    "two arrows" the reporter asked about at 11:25. Neither drives a
    per-edge value.
  - Corner setback arrows (a pool of 6) and face depth arrows (a pool of
    4), shown for the corner current in the tree.
  - A value driven by an expression gets no handle.

### 1.3 The other dress-ups

- Chamfer: `Size`, `Size2`, `Angle`, `ChamferType`, `FlipDirection`, and
  per-edge `ChamferInfo {size, size2, angle, flip}` overriding the
  defaults (`FeatureChamfer.cpp` 140 to 160). Handles on the first edge
  only, driving the defaults.
- Draft: one `Angle` (no per-face values); one rotation handle on the
  first face.
- Thickness: one `Value`; one arrow on the first edge and face.

### 1.4 The machinery

- `Gui::Gizmo` (`src/Gui/Inventor/Draggers/Gizmo.h`): Linear, Rotation,
  DirectedRotation, Radial. A gizmo is bound to a QuantitySpinBox, never to
  a property: it moves when the box changes and writes the box when
  dragged. Hidden while the box has an expression. After a drag the box
  gets the focus with its text selected. `GizmoContainer` takes a FIXED
  list once -- hence the pools.
- `Gui::EditableDatumLabel` (`src/Gui/EditableDatumLabel.h`): a dimension
  label in the view (distance, angle, radius, diameter ...) that turns into
  a QuantitySpinBox over its own number on `startEdit`; signals for typed,
  Enter, Escape, emptied ("unset"), Ctrl+Enter, clicked, focus lost. Fork
  additions: a box anchored over another label's number, and streaming to
  the thin client (`OnViewEntry`).
- It is ALREADY used outside the sketcher in this fork: the pattern
  panels' clickable extent and spacing labels (`PatternWidgets.cpp` 700 to
  822: click to edit, Enter commits, emptied goes back to following, Escape
  or focus loss closes) -- the model for what follows. Also the image
  scale's measured distance (`TaskImage.cpp` 532).
- `Gui::DatumValueEditor` (`DatumValueEditor.h`): an on-view editor that
  takes a value or an `=expression`, a driving/reference toggle and a name;
  used only by the sketcher's constraint editing.
- Drawing under the render engine: gizmos hang under the editing root,
  which is already captured into the `OverlayEditing` overlay; labels hang
  under the on-view root, captured as `OverlayOnView`
  (`View3DInventorViewer.cpp` 684, 693, fed at 1427, 1472). So BY THE
  SOURCE both are fed to the Direct3D render engine with no new capture.
  NOT SEEN RUN: entry 118 (the light's arrow, also fed as an overlay
  capture and not seen) shows a capture can be fed and still not show --
  to be looked at on the screen before relying on it.
- Other features with handles: Pad/Pocket (length, offset, taper),
  Revolution (angles), Helix (height), Hole (depth), the primitives. Only
  the patterns let a value be typed in the view.

## 2. Survey: how other programs do it

| Program | Handle in the view | Value typed in the view | Per edge | Varying along an edge |
|---|---|---|---|---|
| SolidWorks | drag arrows (Instant3D) | a callout with the value beside the edges; click to edit (general knowledge) | FilletXpert "Change" resizes or removes single edges of one fillet feature [1]; "multiple radius fillet" gives each edge its own callout (general knowledge) | "variable size fillet": a radius at each VERTEX of the edges (Attached Radii), "Set All"/"Set Unassigned" [2] |
| Fusion 360 | a radius manipulator arrow to drag, for constant, chord-length and asymmetric fillets [3] | a value box with the manipulator (general knowledge) | edge sets in one fillet, each with its radius (general knowledge) | "Variable": radius points with Radius and Position along a single edge, start and end points, add or remove [3]; points not draggable along the edge in 2017 [4] |
| Inventor | in-canvas controls = manipulators + a value edit control, each with its panel twin [5] | yes, the value edit control [5] | -- | a radius manipulator and a POSITION manipulator per point [5] |
| Onshape | an orange arrow to drag, also for the start and end of a partial fillet [6] | sketch fillets: click the value to type [7]; feature values in the dialog | -- | "variable fillet": values at selected vertices [6] |
| NX | drag handles (general knowledge) | "screen input" boxes for a command's values (general knowledge) | -- | variable radius points, stored as a percent of the whole edge chain [8] |
| Creo | drag handles on the round [9] | double-click the dimension, type, Enter [9] | round SETS, each with its own radius (general knowledge); Creo Direct edits a group of rounds together [10] | -- |
| CATIA V5 | -- | the radius values shown ON the geometry, at the vertices; double-click one to edit [11][12] | -- | values at the vertices; more points added along the edge, each with its value shown [11] |
| SketchUp, Shapr3D | the drag itself | type while dragging: the digits go to the value with no click (general knowledge) | -- | -- |
| FreeCAD upstream | gizmos on dress-ups and pads (1.1) | the sketcher's on-view parameters, Tab from one to the next | -- | -- |

What they share:

1. ONE handle per value, at the place the value acts, and its NUMBER
   shown at the handle (Creo, Inventor, CATIA, Onshape's sketch).
2. The number is the way in for typing: click or double-click it, type,
   Enter. The panel has the same value, and both stay in step (Inventor
   says so outright [5]).
3. Per-edge values are shown at their edges; a varying radius is shown as
   points along the edge, each with a radius and, where it can move, a
   position handle (Inventor, Fusion, CATIA).
4. Values belong to edge GROUPS more often than to single edges
   (SolidWorks multiple radius, Fusion edge sets, Creo round sets) -- a
   value set once for several edges.
5. Fast programs let the keyboard take the value during the drag
   (SketchUp, Shapr3D) and move from value to value with Tab (FreeCAD's
   sketcher, NX).

## 3. The design

### 3.1 One building block: an on-view value

An `OnViewValue` binds ONE value of a feature to the view:

- what: a document object and a property path -- `Radius`,
  `Segments.Edge12[1].Radius`, `Corners.Vertex4.Edge12`, `Size`,
  `Length` -- read and written through the path, with its unit, its
  limits and its expression;
- where: a placement provider (an anchor point and a direction, computed
  from the current shape -- the GizmoHelper functions already compute
  these);
- how: an optional handle (a Gizmo) and a label (an EditableDatumLabel)
  showing the value, the label clickable to type.

It replaces "a gizmo bound to a spin box". The panel's spin box, when the
panel shows that value, is bound to the same path, so the two stay in step
through the property, not through each other. A value with an expression
shows its label in the expression colour and opens the expression on click
(the sketcher's `DatumValueEditor` already takes `=expression`); no handle.

Because a binding is a path, ANY number of them can exist -- one per edge,
one per point along an edge -- and the fixed gizmo pools go: an
`OnViewValueSet` is rebuilt from the feature's properties after each
recompute (where today the fillet panel repositions its pools).

### 3.2 Typing

- A click on a label opens it for typing (the box over its number, as the
  patterns do). Enter commits and recomputes; Escape restores; emptying the
  box means "no value of its own" where that makes sense (3.3).
- Tab and Shift+Tab move to the next and previous label of the feature,
  in a stable order (default first, then edges in the panel's order, then
  corners); Ctrl+Enter commits and leaves the edit (the label's existing
  signal).
- TYPE WHILE DRAGGING: during a handle's drag, a digit key opens that
  handle's label with the digit typed, and the drag stops; Enter commits.
- Each commit is one undo step.

### 3.3 The fillet

- The DEFAULT radius: ONE handle and its label, on one edge that uses the
  default (the first such edge in the panel's order). The label reads like
  "R 2 mm". The other default edges get only a small marker (a dot at
  their middle), clickable to show their own label.
- ONE ARROW per radius, not two. It lies in the face beside the edge that
  is turned more toward the camera, and moves to the other face when the
  camera turns past the edge; the label sits at its tip. (Today's two
  arrows say the same thing twice.)
- A PER-EDGE value: an edge with its own radius shows its own handle and
  label, in the "own value" colour, e.g. "R 3 mm".
  - To GIVE an edge its own value from the view: open the label of a
    default edge (click its marker, then its label) and type -- the edge
    gets one point `(0, value)`. To GIVE IT BACK to the default: empty the
    label (the patterns' "unset" already means this).
- TWO RADII and VARYING radii: an edge with several points shows a label
  per point at its position along the edge, and two handles per point: the
  radius arrow and a POSITION handle that slides along the edge (Inventor's
  position manipulator). A label's context menu: "Vary along this edge"
  (one point becomes two, at the start and the end, both the current
  radius), "Add a point here", "Remove this point", "Use the default".
  The position is typed as a fraction or, with a unit, as a length
  (`Length` wins over `Param`, as the property has it).
- CORNERS: the setback and depth handles stay as they are, each with its
  label; shown for the corner the pointer is over or the panel has current.
- CLUTTER: with many edges, show the labels of (a) the default, (b) every
  edge with its own value, (c) the edge under the pointer -- and markers
  for the rest. A setting may show all.

### 3.4 The other dress-ups

- Chamfer: the same as the fillet, with up to three values per edge (size,
  size2, angle) and a click on the edge's direction marker for "flip";
  per-edge values through `ChamferInfo` exactly as the fillet's through
  `Segments`.
- Draft: one angle handle and label; Thickness: one value handle and
  label. Only the move to 3.1 and 3.2.

### 3.5 Without the task panel

The reporter's "in general": a value entered in the view WITHOUT the task
panel. Two steps, the second built on the first:

1. IN AN EDIT, panel optional. All of 3.2 to 3.4 works with the panel
   collapsed or hidden (the overlay's auto-hide already hides it); the
   view alone is enough to change every value the panel has.
2. OUTSIDE AN EDIT. A view provider can list its feature's on-view values
   (`ViewProvider::getOnViewValues()`, built from the same bindings). With
   one feature selected, a double click on the feature in the view -- or a
   key -- shows its values' labels in the view without opening the panel
   (Creo shows a feature's dimensions this way); a click on a label types,
   Enter commits and recomputes as one undo step; Escape or a click
   elsewhere hides them. The dress-ups first, then Pad/Pocket (length,
   offset, taper), Revolution (angles), Hole (depth), Helix, the primitives
   -- each already has handles placed by its panel, to be moved into its
   view provider.

### 3.6 Settings

- The existing gizmo switch stays (handles on or off).
- New: "Values in the view" -- off / in an edit only / also on a selected
  feature (step 3.5.2), as the sketcher's on-view parameter visibility is a
  setting.
- New: "Show the labels of" -- the default and own values / every edge.

### 3.7 Thin client and drawing

Labels are `OnViewEntry`s already, so a browser client gets them through the
existing stream; handles draw through the editing overlay. By the source
nothing new is needed for the Direct3D render engine -- a claim until it is
seen on the screen (1.4, and entry 118).

## 4. Order of work

1. `OnViewValue` and `OnViewValueSet` (3.1) and typing (3.2), with the
   fillet's DEFAULT radius as the first user -- one arrow, its label.
2. The fillet's per-edge values: own value, give back to default, varying
   points with position handles, the context menu (3.3).
3. Chamfer, Draft, Thickness moved to it (3.4).
4. Panel optional (3.5.1).
5. Outside an edit (3.5.2), feature by feature.

Each step is shown on the real screen by a picture and by typed values, as
the hands-on queue asks.

## 5. For the reporter to decide

a. ONE arrow per radius, on the face turned to the camera -- or keep the
   two?
b. Which labels show at once on a fillet with many edges: the default,
   the own values and the edge under the pointer (proposed) -- or all?
c. How a varying radius is made in the view: the label's context menu
   (proposed) -- or a drag along the edge with a modifier key?
d. Type while dragging (3.2): wanted?
e. Outside an edit (3.5.2): shown by a double click on the feature, by a
   key, or whenever one feature is selected?
f. Edge GROUPS (one value for several edges, as SolidWorks, Fusion and
   Creo have): wanted? The fork's `Segments` is per edge; a group would be
   a new property or a shared expression.

## Sources

1. SolidWorks, FilletXpert Change PropertyManager:
   https://help.solidworks.com/2018/english/SolidWorks/sldworks/HIDD_FILLET_MGR_EDIT.htm
2. SolidWorks, Variable Size Fillet:
   https://help.solidworks.com/2016/English/SolidWorks/Sldworks/r_Variable_Size_Fillet.htm
3. Fusion, Create a fillet:
   https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/SLD-FILLET-SOLID.htm
4. Autodesk forum, variable radius fillet point via slider (2017):
   https://forums.autodesk.com/t5/fusion-360-ideastation-archived/variable-radius-fillet-move-point-via-slider/m-p/7033813
5. Inventor, About Direct Manipulation of Objects:
   https://help.autodesk.com/cloudhelp/2026/ENU/Inventor-Help/files/GUID-B77F3FAC-5837-4C06-823A-735DB508248B.htm
6. Onshape, Fillet: https://cad.onshape.com/help/Content/PartStudio/fillet.htm
7. Onshape, Sketch Fillet: https://cad.onshape.com/help/Content/sketch-tools-fillet.htm
8. Siemens, variable radius points by percent arc length:
   https://support.sw.siemens.com/en-US/okba/PL8015439/The-variable-radius-points-cannot-be-specified-by-percent-Arc-Length-of-each-edge/index.html
9. Creo, To Edit Round Geometry:
   https://support.ptc.com/help/creo/creo_pma/r13/usascii/part_modeling/flexible_modeling/to_edit_round_geometry.html
10. Creo Direct, To Edit a Round:
    https://support.ptc.com/help/creo/creo_dma/r7.0/usascii/dma/direct_modeling/to_edit_rounds.html
11. CATIA V5 tutorial, Variable Radius Fillet:
    https://www.staff.city.ac.uk/~ra600/ME2105/Catia%20course/CATIA%20Tutorials/prtug_C2/prtugbt0609.htm
12. IBM support, APAR HD54873 (double-click a variable fillet's radius
    value): https://www.ibm.com/support/pages/apar/HD54873
