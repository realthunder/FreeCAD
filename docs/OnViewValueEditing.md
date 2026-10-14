# On-view value editing: dress-up handles and typed values in the 3D view

A design proposal, not built; revised the same day with the reporter's
answers a to f (section 5). Asked by the reporter on 2026-10-14
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
  showing the value, the label clickable to type;
- what else: a list of ACTIONS for the handle's root button (3.3) --
  each a title, a checked state where it is a choice, and what it does --
  given by the feature's panel (later by its view provider).

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
- THE ACTIVE HANDLE TAKES TYPING (decided 2026-10-14, d): a handle made
  active by Tab or a click is highlighted and shows its label beside it; a
  digit typed then goes into that label without a further click. No typing
  during a drag.
- KEYS WHILE A HANDLE IS ACTIVE (decided by the reporter 2026-10-14
  13:28): SPACE opens the handle's context menu, as if its root button
  were pressed; ENTER starts typing in the handle's dimension (its label
  opens with the current value selected). Only while a handle is active:
  otherwise Space keeps its own meaning -- today "Toggle visibility"
  (`StdCmdToggleVisibility`, `CommandView.cpp` 951) -- and Enter its own.
  Once the label is open, Enter commits (as above) and Escape restores
  and leaves the handle active.
- Each commit is one undo step.

### 3.3 The fillet

DECIDED by the reporter 2026-10-14 13:21 (a, b, c): one arrow; the default
radius's label always at the top middle of the view, the others only
beside their handle when it is active; a pressable at the root of each
arrow -- made at 13:25 a BUTTON opening a context menu (the mode, remove,
and each feature's own operations). This section is written to those
answers.

- THE DEFAULT RADIUS: its label is ALWAYS shown, at the TOP MIDDLE of the
  view (a fixed place on the screen, not on an edge), e.g. "Radius 2 mm";
  click or Tab to it and type.
- EVERY OTHER LABEL is shown only BESIDE ITS HANDLE, and only while that
  handle is ACTIVE -- made so by Tab or by a click on it; the active handle
  is drawn highlighted. No other labels, so a fillet of many edges shows
  its handles and one number at most beside them.
- ONE ARROW per radius (a). It lies in the face beside the edge that is
  turned more toward the camera, and moves to the other face when the
  camera turns past the edge. (Today's two arrows say the same thing
  twice.)
- THE ROOT BUTTON (c, made general by the reporter at 13:25 -- it
  replaces the cycling toggle first proposed): the ROOT of each handle is
  drawn as a BUTTON; a click on it opens a CONTEXT MENU of what can be done
  with that value. For the fillet's edge arrow:
  - the MODE, as three checkable entries, the current one checked:
    1. "Default radius" -- the edge takes the default (no entry of its
       own; its arrow, if dragged, drives the default);
    2. "One radius" -- one point `(0, r)`, `r` starting at the radius it
       had;
    3. "Two radii" -- two points, at the start and the end of the edge,
       both starting at the radius it had; a second arrow at the far end
       of the edge for the end radius, with its own root button;
  - "Group" -- a sub-menu: this feature's groups by id with their values,
    and "New group" (section 6); shown for a customised edge;
  - "Remove" -- the edge is taken out of the fillet (the panel's remove).
  An edge with MORE than two points (possible in the file and in the
  panel) shows its points' arrows and a menu whose mode reads "Varying
  (set in the panel)", not one of the three.
  The look tells the modes apart -- see VISUAL HINTS below.
- VISUAL HINTS: DEFAULT and CUSTOMISED edges told apart (asked by the
  reporter 13:47; the note-taker's proposal, three layers that work
  together, each readable without the others):
  1. THE ROOT BUTTON'S SHAPE says the mode: DEFAULT -- a HOLLOW ring;
     ONE RADIUS -- a FILLED disc; TWO RADII -- a filled disc at EACH end
     of the edge (one per arrow); VARYING (more than two points, set in
     the panel) -- a filled disc with a small tick ring. Shape, not colour
     alone, so it reads for colour-blind users and in a monochrome
     picture.
  2. COLOUR says default or group: default handles (ring and arrow) in
     the theme's NEUTRAL handle colour, drawn thinner and at reduced
     opacity -- they are many and all mean one value, the one at the top
     middle; customised handles SOLID, in their GROUP's colour, taken from
     a short palette by group id (id 1 the first colour, and so on), so
     that the members of one group match at a glance; the group id
     itself small inside the filled disc. The active handle and its group
     are drawn highlighted over this (3.3, section 6).
  3. THE EDGE (optional, a setting): a customised edge drawn with a thin
     band in its group's colour along it while editing, so a group can be
     seen without hunting for its handles.
  A default edge's arrow, dragged, drives the default radius -- its
  neutral look says that it is the shared value.
- OTHER FEATURES put their own operations in the same menu, on the same
  button (3.4 for the dress-ups). For instance -- the note-taker's
  examples, to be settled feature by feature: Pad/Pocket length --
  "Reversed", "Symmetric to plane", the length type ("Dimension",
  "Through all", "Up to face" ...); Revolution angle -- "Reversed",
  "Symmetric"; Hole depth -- "Reversed", "Through all"; Draft -- "Reverse
  pull direction"; Thickness -- "Reversed", the join type. Today several
  of these handles toggle "Reversed" on a plain click (the gizmos' click
  callback, `TaskExtrudeParameters.cpp` 242 to 244, Revolution, Hole); the
  menu takes that over, so a click on the arrow itself no longer flips
  anything by surprise.
- HOW OTHER PROGRAMS SWITCH this (asked by the reporter, c), from the
  survey: none found has a button on the handle. Fusion has a type per
  edge set (Constant / Variable ...) in the dialog [3]; SolidWorks a
  Constant size / Variable size choice and a "multiple radius" option
  (general knowledge, [2]); Onshape a "Variable fillet" check box [6];
  CATIA a variable option and points added on the edge [11]; Creo, by an
  old forum answer, a right-click on the radius handle and "Add radius"
  for a variable round [13]. So the switch on the handle itself is this
  fork's own -- closest to Creo's right-click on the handle.
- CORNERS: the setback and depth handles stay as they are; their labels
  show, like the others, when a handle is active.
- POINTS ALONG AN EDGE: the arrow of a point that is not at an end gets
  a POSITION handle as well, sliding along the edge (Inventor's position
  manipulator, [5]); its label, when active, takes the position as a
  fraction or, with a unit, as a length (`Length` wins over `Param`, as
  the property has it). Points are added and removed in the panel.

### 3.4 The other dress-ups

- Chamfer: the same as the fillet -- the default values' label at the top
  middle, per-edge values through `ChamferInfo` as the fillet's through
  `Segments`; the root button's menu: the mode ("Default size", "Own
  size", "Own two sizes" or "Own size and angle", by `ChamferType`),
  "Flip direction", "Remove".
- Draft: one angle handle, its menu "Reverse pull direction", "Remove"
  (the face); Thickness: one value handle, its menu "Reversed", the join
  type, "Remove" (the face); their label at the top middle.

### 3.5 Without the task panel

The reporter's "in general": a value entered in the view WITHOUT the task
panel.

1. IN AN EDIT, panel optional. All of 3.2 to 3.4 works with the panel
   collapsed or hidden (the overlay's auto-hide already hides it); the
   view alone is enough to change every value the panel has.
2. OUTSIDE AN EDIT -- DROPPED by the reporter (13:46, "yes, drop").
   The reporter had asked (e, 13:21):
   "double-click is entering edit right? so what's the point of modify
   without entering edit". Answered in the session: Creo's plain "Edit"
   and SolidWorks' double-click on a dimension change values without the
   feature's full edit, and it would work here as a property write and a
   recompute, as the property view already does; but in FreeCAD a double
   click already IS the edit, and with step 1 the edit itself takes typed
   values in the view with the panel hidden -- so the gain is small.

### 3.6 Settings

- The existing gizmo switch stays (handles on or off).
- New: "Values in the view while editing" -- on (default) / off.

### 3.7 Thin client and drawing

Labels are `OnViewEntry`s already, so a browser client gets them through the
existing stream; handles draw through the editing overlay. By the source
nothing new is needed for the Direct3D render engine -- a claim until it is
seen on the screen (1.4, and entry 118).

## 4. Order of work

1. `OnViewValue` and `OnViewValueSet` (3.1), the active handle and typing
   (3.2), with the fillet's DEFAULT radius as the first user -- one arrow,
   its label at the top middle.
2. The fillet's per-edge values: the root button and its menu (mode,
   remove), the second arrow at the far end, position handles (3.3).
3. Edge groups as the reporter decided them (section 6).
4. Chamfer, Draft, Thickness moved to it (3.4).
5. Panel optional (3.5.1).

Each step is shown on the real screen by a picture and by typed values, as
the hands-on queue asks.

## 5. Decided, and what is left

The reporter, 2026-10-14 about 13:21: "a, one. b, the default radius
always show in the top middle. others only show besides the handle when
Tab or clicked (show highlight for the active handle). c, How other CAD
does this? my thoughts are something pressable, maybe sit at the root of
the arrow handle. each press toggle a state between default, per edge one
radius, two radius. d, no need, active handle accepts typing is enought, I
think. e, double-click is entering edit right? so what's the point of
modify without entering edit. f, show me how would you implement it."

- a: ONE arrow -- 3.3.
- b: the default's label at the top middle, the others beside an ACTIVE
  handle only, highlighted -- 3.3.
- c: the root of each handle a BUTTON opening a context menu -- for the
  fillet the mode (default / one radius / two radii) and "Remove", other
  features their own operations (the reporter's second thought, 13:25:
  "or, maybe that's better and more general, make the root of the handle
  pressable, like show it as a button, when clicked show the context menu
  to selection various options. for fillet, that's the mode selection
  (default, per edge one radius, two radius), and a remove action. for
  other features put their own operations") -- 3.3, with the survey's
  answer to "how other CAD does this".
- d: no typing during a drag; the active handle takes typing -- 3.2.
- e: outside an edit DROPPED (13:46) -- 3.5.2.
- f: edge groups DECIDED by the reporter's own rule (13:46): an
  automatic group id per customised edge, joined by changing it from the
  menu, every value change mirrored to the group, the group highlighted
  when a handle is active -- section 6.

LEFT for the reporter: the other features' menu entries (3.3, the
note-taker's examples), feature by feature; and the note-taker's smaller
proposals in section 6 (an expression conflict inside a group, the
"reversed in its group" flag, Tab unchanged).

## 6. Edge groups: DECIDED by the reporter (2026-10-14 13:46)

The reporter's words: "about the group, my idea is for all customized edge
store a new group id auto assigned when first enter the customization
mode. user can change the group id to join. in the context menu add a
selection for group. when an edge's radius (one or two, or point ratio,
whatever), mirror the same change to all edge with the same group id. when
activate a handle (by click or tab), hight all in the same group.
enter/space act only on the actual activated handle, but the value change
will be mirrored just the same"

The two forms first written here (a group table inside `Segments`, or
shared expressions) are REPLACED by this one.

### 6.1 The rule

- Every CUSTOMISED edge -- one whose mode is not "Default radius" --
  carries a GROUP ID.
- When an edge FIRST leaves the default (its menu's "One radius" or "Two
  radii"), it gets a NEW group id of its own, assigned automatically (the
  lowest id not in use on this feature). So by default every customised
  edge is a group of one.
- The user may CHANGE an edge's group id to JOIN another group: the root
  button's menu has a "Group" entry, a sub-menu listing the groups of
  this feature by id (with their values beside them, e.g. "2 -- R 3 mm")
  and "New group". Joining a group takes that group's values.
- A CHANGE to any value of an edge -- its radius, its two radii, a
  point's position (param or length), whatever the edge holds -- is
  MIRRORED to every edge with the same group id, the same change.
- ACTIVATING a handle (a click or Tab) HIGHLIGHTS every edge of its
  group; the active handle itself is drawn as the active one.
- Enter and Space act only on the ACTIVE handle (its label, its menu);
  the change they make is mirrored all the same.
- An edge going back to "Default radius" leaves its group (its id is
  dropped); a group whose last member goes is gone.

### 6.2 How it is stored

- Each edge keeps its OWN values as it does today, in `Segments`; the
  group id is one more thing stored per edge: `<Segments id="Edge12"
  group="2" count="1">...`. `Restore` reads `group` when it is there.
- So the FILE stays complete without the groups: every member carries
  its values. An older build reads the values, ignores the id, and makes
  the same fillet -- only the mirroring is lost there. Nothing is lost on
  a re-save by the new build.
- The group id rides on the per-edge entry, so it follows the edge when
  the edge is renamed upstream, with the entry itself
  (`connectLinkProperty` -- once the build session's fix of 13:45 is in,
  6.5).
- Python and expressions: `Segments.Edge12.Group` as a path, read and
  written like the per-point paths; a helper on the feature,
  `setEdgeGroup(edge, id)`.

### 6.3 Where the mirroring happens

- In the property, not in the GUI: `PropertyFilletSegments` gets a
  setter that writes an edge's points and copies them to every edge with
  the same group id, in one change (one recompute, one undo step). The
  panel, the on-view handles and Python all write through it, so a value
  set from any of them is mirrored the same way.
- An EXPRESSION on one member's value: mirrored by the same setter when
  the expression's value changes (the expression engine writes through
  the property's path setter). Expressions on two members of one group
  that disagree are a conflict; the note-taker's proposal: the newest
  write wins and the panel marks the group's other expressions.
- TWO RADII across edges: "start" and "end" follow each edge's own
  direction, which differs between edges, so a mirrored (r1, r2) can land
  reversed on some members. The note-taker's proposal: each member keeps
  a "reversed in its group" flag (saved as `rev="1"`), set by a "Reverse"
  entry in that member's menu, and the mirror swaps for it. Point
  positions (param) mirror as 1 - param on a reversed member.

### 6.4 In the view and the panel

- View: each member edge has its own handle(s) as before; the active
  handle's group is highlighted; the label shows beside the active handle
  only, as for any edge, with the group id, e.g. "2  R 3 mm".
- Panel: a "Group" column in the edge tree, an editable number; the same
  mirroring when a value is typed there.
- Tab: visits every handle as before (not once per group) -- the
  reporter's rule says Enter and Space act on the active handle; the
  note-taker took Tab as unchanged.

### 6.5 Tests

`TestFillet.py`: two edges customised get two ids; one joins the other's
group and takes its values; a radius changed on one is mirrored; two radii
with a reversed member; a point's param mirrored (1 - param when
reversed); back to default leaves the group; save and restore keep the
ids; an older file (no ids) restores with every customised edge on its
own; an edge renamed upstream keeps its id (after the build session's
rename fix). GUI: the group highlighted on activation; a value typed at
the active handle reaching every member.

Chamfer: the same, on `PropertyChamferEdges` (size, size2, angle, flip
mirrored; flip itself per member).

### 6.6 Seen in the source on the way, not part of this design

`PropertyFilletSegments::connectLinkProperty` (`PropertyDressUp.cpp` 184
to 197), as first read by the note-taker: both `p1` and `p2` built from
the OLD path, and the index stepped twice. CONFIRMED by the build session
(13:45) and WIDER: the whole rename map is backwards, new -> old, in
`Segments` and in `PropertyChamferEdges` alike, so an expression on a
per-edge value never follows a renamed edge. The build session fixes it
on its own judgement (a test `TestDressUpNamesFollow`).

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
13. PTC Community, variable round (2008 forum answer, "add radius"):
    https://community.ptc.com/creo-modeling-questions-5/variable-round-chamfer-38079
