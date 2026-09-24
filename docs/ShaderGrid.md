# A grid drawn per view

Status: design, nothing built. Written 2026-09-24 (session 93), after the
wasted-republish chase (`3a2973ae9b`); the open questions were answered the
same day and are recorded in section 10. **Scheduled after the Sketcher
port** (user, 2026-09-24): no grid work starts until that port is done.
Supersedes nothing.

One grid primitive, drawn by whichever camera is looking at it, shared by
every workbench that shows a construction grid today: the Sketcher (through
Part's `ViewProviderGridExtension`) and Draft/BIM (`gridTracker`). The ask
that started it was "render the grid with a shader, the way Blender does";
section 2 is why the recommendation is to generate per-view **lines** rather
than shade a full-screen plane, which is also where Blender itself has since
gone.

## 1. Problem

Two grids, two opposite defects, one shared root: the lattice is built as
Coin geometry for one camera.

**The sketch grid rebuilds on every zoom.** `GridExtensionP` (in
`src/Mod/Part/Gui/ViewProviderGridExtension.cpp`) attaches an `SoNodeSensor` to
the camera. On any change of `viewer->getMaxDimension()` -- any zoom at all --
or a pan of more than 10% of it, `createGrid()` removes every child of
`GridRoot` and builds two new `SoLineSet`s (minor, and major every
`GridNumberSubdivision`), sized `1.5 * camMaxDimension / gridValue`, with the
spacing stepping by the subdivision factor as the zoom crosses thresholds.

Measured in session 93 with the scene server up (node types logged on every
feed of the sketch edit overlay): during the ten frames of a `fitAll`, 25 of
the overlay's 27 draws stayed the same objects and the other two were **new
nodes every frame** -- the grid's line sets, 431, 57, 69, 79 ... 131
vertices. Each one is a real content change, so each republished the scene:
13 publishes for one fit, after the unrelated headlight waste was removed.
It also means:

- **A streamed viewer sees the desktop's grid.** Spacing and extent are
  chosen for the desktop camera. A browser viewer zoomed differently gets a
  grid that is too dense, too sparse or clipped, and it changes whenever the
  *desktop* zooms.
- The desktop pays too: a node rebuild, a render-cache recapture and a
  re-feed per camera step, for what is conceptually a constant.

**The Draft grid never follows the camera.** `gridTracker`
(`src/Mod/Draft/draftguitools/gui_trackers.py`) builds a fixed lattice once
per working-plane or preference change: `gridSpacing`, a main line every
`gridEvery`, `gridSize` lines across, plus axis lines coloured by which world
axis the working plane's u/v follow, and, with `gridBorder`, a border, tick
"cursors", two size labels and a human-scale figure. Zoomed out it moires
into a grey sheet; zoomed in, a handful of lines cross the screen. BIM
inherits it (`BimProjectManager.py`).

## 2. Prior art

**Blender, 2.80 to 4.x: a fragment-shader grid.** One large plane per grid
(the floor in perspective views, the view plane in ortho views);
`overlay_grid_frag.glsl` computes each pixel's distance to the nearest line
on the plane, using `fwidth()` of the plane position to keep lines about one
pixel wide and antialiased at any zoom. The CPU uploads a few spacing
levels (following the scene's unit system) and the shader blends adjacent
levels, so spacing changes fade instead of popping; alpha also fades with
distance toward the far clip and at grazing angles
(`1 - pow3(1 - |V.z|)`), which is what hides the moire.

**Blender, since: lines generated per view.** Issue #130464 ("Overlay:
Speed-up grid drawing") states the problem with the above: "a giant plane
mesh with a very expensive fragment shader that is executed on each pixel the
grid touches (which is most of the time the whole screen)", plus a depth
texture read per pixel for its soft depth test. It proposed line geometry:
better GPU occupancy, the same line antialiasing as every other overlay,
correct depth merging with the other overlays, and "less lines than pixels
on the screen". PR #148733 did it, and current `main` has it:
`overlay_grid_vert.glsl` generates every line from `gl_VertexID` with no
vertex buffer (bit 0 = end of the line, bit 1 = x or y family, the rest =
line and level). Per-level steps and a fractional level come in as uniforms;
the grid centre snaps to the step so the lattice stays put while the camera
moves; `alpha = saturate(level + 1 - fract(grid_level))` fades a level out
and `emphasis` fades the next one in.

What the current Blender grid does per view, read from `overlay_grid.hh`,
`overlay_grid_vert.glsl`, `overlay_grid_frag.glsl` and
`overlay_shader_shared.hh` on `main` -- this is what sections 6.1 and 6.2
follow:

- **One level per view, not one per distance band.** A single fractional
  level comes from one camera distance. In perspective that distance
  blends the distance along the view ray to the plane with the camera's
  height above it, by how steeply the view looks down
  (`interpolate(|pos.z / fwd.z|, |pos.z|, 1 - |fwd.z|)`); in an ortho view
  it comes from the viewport width and the projection scale. The level
  indexes a table of steps (the scene's units; a missing step is the
  previous one times 10).
- **Three adjacent levels drawn** (`OVERLAY_GRID_STEPS_DRAW = 3`),
  **151 lines each in perspective, 301 in ortho**, centred on the point
  where the view meets the plane (perspective:
  `camera_pos - dist * forward`), each level snapped to its own step,
  clipped to a square sized from the camera's far clip.
- **The horizon is a fade, not a cut.** Alpha falls to zero over the far
  half of the clip range
  (`1 - smoothstep(0, far/2, d - far/2)`) and at grazing angles
  (`1 - pow3(1 - |V.z|)`). In ortho each level also fades as its spacing
  shrinks on screen (`smoothstep(step/4, step/64, pixel_size)`), which is
  what stops a dense level turning into a grey sheet.
- **Depth.** Test `LESS_EQUAL`, alpha blend, additive. In perspective the
  same lines are drawn **four times** (`OVERLAY_GRID_ITER_LEN = 4`) with a
  depth bias stepping from +0.00025 to -0.0002 and alpha 1, 0.5, 0.25,
  0.125; only the first pass writes depth. Where a grid line meets a
  surface, the passes give up one by one, so the line fades into the
  surface instead of cutting off: a soft depth test with no depth-texture
  read, the per-pixel cost #130464 complained about. In an axis-aligned
  ortho view (top, front, side) the grid is flagged
  `GRID_BEHIND_GEOMETRY` and pushed to the far plane: a background behind
  everything, drawn once.
- **Axes.** Axis lines are drawn in the same pass with their own colours;
  where an axis coincides with a grid line, the grid line is discarded so
  the two do not double up.

**Ben Golus, "The Best Darn Grid Shader (Yet)" (2023).** The reference for
doing the fragment-shader version well: box-filtered lines from derivatives,
"phone-wire" antialiasing (a line thinner than a pixel is drawn one pixel
wide at reduced alpha rather than breaking up), and fading to the average
colour where the lines outnumber the pixels.

**What carries over.** The point of "do it in a shader" is not the fragment
shader. It is that the lattice is a **function of a few parameters and the
camera**, evaluated where the camera is, so every view gets its own correct
grid and nothing about it is ever rebuilt or shipped. Blender's current
design keeps exactly that and moves the work from every pixel to a few
thousand line vertices. This engine already has a thick-line path with
per-pixel widths, analytic coverage, stipple and clip planes
(`vs_fc_line.sc` and friends), so the line form is also the one that costs
least here and matches how every other edge already looks.

## 3. Goals and non-goals

Goals:

1. One primitive both workbenches use; no grid geometry in any
   workbench's scene graph.
2. The lattice is computed per view, per frame, from parameters that do not
   depend on the camera. A zoom or pan changes nothing in the scene, so it
   republishes nothing, and a streamed viewer draws the grid for its own
   camera.
3. Both kinds of grid: **automatic levels** (the sketch grid's `GridAuto`,
   and optionally Draft's) and **fixed** (Draft today, sketch with
   `GridAuto` off), bounded or unbounded.
4. Snapping stays exact: the spacing a workbench snaps to is the spacing
   drawn, from one shared function.
5. The Coin-only path (render-cache mode below 3, no renderer) keeps
   working, from the same node.
6. Portable: nothing that WebGL2 or a mobile GPU lacks.

Non-goals: Draft's border, tick marks, size labels and human figure (they
stay ordinary Coin geometry beside the grid; they only change when the
working plane or the preferences do); Sketcher's own root cross; a
Blender-style global floor grid in every 3D view (a natural later client of
the same node, section 9, not part of this).

## 4. The primitive: `SoFCGrid`

A Coin shape node in `src/Gui/Inventor/`, registered with the other `SoFC*`
nodes. The grid lies in the node's local XY plane at z = 0; placement comes
from whatever transform is above it, as both grids already do. Fields:

| Field | Meaning | Sketcher (today's source) | Draft (today's source) |
| --- | --- | --- | --- |
| `spacing` | base (finest) step, model units | `GridSize` | `gridSpacing` |
| `majorEvery` | a major line every N minor ones | `GridNumberSubdivision` | `gridEvery` |
| `autoLevels` | pick the step from the camera | `GridAuto` | false (decided, see 10) |
| `levelFactor` | step ratio between levels; 0 = `majorEvery` | as today (`<= 1` -> 10) | -- |
| `minPixelSpacing` | finest step allowed on screen, px | `GridSizePixelThreshold` (15) | -- |
| `halfExtent` | bounds in u, v; 0 = unbounded | 0 | `numlines/2 * spacing` |
| `minorColor`, `majorColor` | RGBA, alpha = the transparency | `GridLineColor`/`GridDivLineColor` + `GridTransparency` | `gridColor`, alpha `0.8*(1-t)` / `0.2*(1-t)` |
| `minorWidth`, `majorWidth` | px | `GridLineWidth`/`GridDivLineWidth` | 1 / 1 |
| `minorPattern`, `majorPattern` | 16-bit stipple | `GridLinePattern`/`GridDivLinePattern` | solid |
| `axisColors` | colours of the u = 0 and v = 0 lines; alpha 0 = no axis lines | none | `setAxesColor()` result |
| `fade` | perspective distance and grazing-angle fade | on | on |

It is unpickable and contributes no bounding box, as both grids now get from
`SoSkipBoundingGroup`, so a grid never moves `fitAll`.

Python reaches it the way Draft already reaches `SoFCSelection` and CAM
`SoFCPlacementIndicatorKit`: `coin.SoType.fromName("SoFCGrid").createInstance()`
and then plain field access (`grid.spacing.setValue(...)`). No new binding,
and it lives in FreeCAD, not the Coin fork, so no pivy rebuild.

### 4.1 One function for the levels

A header in the renderer library, free of App/Gui types
(`src/Gui/Renderer/GridLevels.h`, namespace `Render`): given the spec and
**the size of one model unit on screen at the grid** (pixels per unit), return
the fractional level, the step of each drawn level, and the alpha of each.
Three callers, one answer:

- the backend, per view (section 6);
- the node's Coin-only draw (section 5.1);
- snapping: `ViewProviderGridExtension::getGridSize()` answers from it
  instead of from `computeGridSize()`, so the sketch snaps to exactly the
  lines drawn -- **for the camera of the view the pointer event came
  through** (decided, section 10). On the desktop that is the edit view.
  For a browser viewer driving the edit it is that client's mirror viewer,
  which already holds the client's own camera (the `'C'` uplink frame,
  `SceneCameraFrame` in `SceneServer.h`, docs/ThinClient.md sec 8.5). So
  `getGridSize()` gains a viewer argument and the snap path passes the
  viewer that dispatched the event; the grid a user snaps to is always the
  grid on their screen.

The level rule reproduces today's sketch grid spacing
(`spacing * factor^(1 + floor(log_factor(viewSize / lineCount / spacing)))`)
at integer levels, and adds the fractional part that drives the fade.

## 5. Two ways it is drawn

### 5.1 Coin only

`SoFCGrid::GLRender` reads the view volume, intersects it with the plane,
asks `GridLevels` for the levels and draws the lines itself. Reading
`SoViewVolumeElement` makes any enclosing separator cache depend on the
camera automatically, so render caching stays correct. Nothing in the
scene graph is edited and nothing notifies: the node-rebuild storm goes away
for Coin users too.

### 5.2 Under the renderer (mode 3)

The render cache captures the node through a companion, the recipe
`SoFCImageQuad` already uses (`SoFCRenderCacheManager.cpp`): it emits only
what does not depend on the camera. That is one `Material::Line` draw whose
material carries the spec (a `std::shared_ptr<const GridSpec>`, the way
`finishpalette` rides along), on a tiny proxy mesh holding the bounded
rectangle, or nothing, for an unbounded grid. The capture is identical for
every camera, so it stays cached, and a zoom produces **no feed and no
publish**.

## 6. Backend

When a view submits a draw whose material has a grid spec, instead of
binding the mesh's static segment buffer it:

1. Works out the grid's camera distance and centre the way Blender does
   (section 2): in perspective, the blend of the distance along the view ray
   to the plane and the camera's height above it, centred where the view
   meets the plane; in ortho, from the viewport width and projection scale,
   centred on the view. A bounded grid clamps the window to `halfExtent`.
2. Asks `GridLevels` for the one fractional level that distance gives, and
   draws that level and the two above it. An unbounded grid uses Blender's
   fixed line counts (151 per level in perspective, 301 in ortho) around the
   centre, each level snapped to its own step so the lattice stays fixed in
   model space while the camera moves. A fixed grid (`autoLevels` off) has
   one level, its own `spacing`, over its own extent.
3. Writes the segments into a **transient** instance buffer: the same
   per-instance layout `vs_fc_line` already reads (endpoints, stipple run,
   colour at each end). Level fade and major emphasis, axis colours, and
   Blender's fades -- far-clip distance and grazing angle in perspective,
   on-screen spacing in ortho -- all go into those per-end colours, so the
   existing line shaders need no change. Where an axis line and a grid
   line coincide, only the axis line is emitted.
4. Submits through the existing thick-line programs (`m_progLine`,
   `m_progLinePat`, `*Clip`), so widths, analytic antialiasing, stipple and
   clip planes are the ones every other line has. Depth is section 6.1.

The spacing fade applies to a fixed grid too: its minor lines fade out as
they close up on screen, then its major ones. That is rendering only -- the
spacing, the extent and what snaps are unchanged -- and it is what replaces
Draft's moire when zoomed out.

### 6.1 Depth, as Blender does it (decided, section 10)

- **In general** (perspective, or an ortho view not looking straight down
  the grid's normal): depth test `LESS_EQUAL`, alpha blended, and the
  segments submitted four times with the bias and alpha stack of section 2
  (+0.00025 to -0.0002; 1, 0.5, 0.25, 0.125), only the first writing depth.
  The bias goes in `u_params.z`, which `vs_fc_line` already applies as an
  NDC depth bias for the outline passes, so this needs no shader change.
  Four submits of one transient buffer, not four buffers.
- **Looking straight down the normal in ortho** (a sketch viewed normal to
  its plane, Draft's working plane from top): behind geometry -- one
  submit, depth pushed to the far plane, no depth write. Another
  `u_params` value, or a clamp in the depth state; phase 2 decides which.
  "Straight down" is a tolerance on the angle between the view direction
  and the plane normal, and the switch between the two modes is where a
  visible jump could show; phase 2 checks it by rotating across the
  threshold.

To check in phase 2, because it is the one place this changes what users
see today: a sketch on a solid's face viewed normal to the sketch puts the
grid behind the face, so the face hides it, where today's depth-tested,
coplanar grid lines show on it or z-fight with it. That is what Blender
does with its floor grid and a model lying on it, and it is the behaviour
decided; the check is only that the sketch edit's own geometry, which is
drawn in the edit overlay, still draws over the grid.

The segment list is memoised on the view's camera hash (the one the static
frame cache already computes), so a still camera reuses it. A per-level line
cap replaces today's 2000-line warning; past it the level is dropped rather
than drawn as a grey sheet.

Everything above is CPU arithmetic plus a transient buffer, which every bgfx
backend has, WebGL2 included. The Blender-style `gl_VertexID` generator is a
possible later optimisation, not a requirement.

## 7. On the wire

The grid spec is serialized with the material in `writeMaterial`, so it gets
content-key dedup for free: next SceneDump version (80) and a `kChunkVersion`
move. The browser viewer runs the same backend code, so it draws the grid
for **its** camera with no viewer-specific work. A grid costs a few dozen
bytes once, instead of two meshes per desktop zoom step.

## 8. Moving the clients over

**Sketcher, via `ViewProviderGridExtension`.** Keep the class, every
property and every preference. Replace `createGrid()`, `createGridPart()`
and the camera sensor with one `SoFCGrid` under `GridRoot`, whose fields
are set from the properties. `getGridSize()` answers from `GridLevels` for
the edit view's camera (section 4.1). `getClosestGridPoint()` is unchanged.
Net: the sensor, the rebuild and the 2000-line special case disappear.

**Draft (and BIM), via `gridTracker`.** Replace `coords1/lines1`,
`coords2/lines2` and the axis `coords3/lines3` with one `SoFCGrid` (fixed
levels, `halfExtent` from `numlines`, `majorEvery = mainlines`,
`axisColors` from `setAxesColor()`). The border, ticks, labels and human
figure stay as they are. `update()` shrinks to setting fields;
`getClosestNode()` is unchanged because a fixed grid snaps to `space` as
before. BIM needs nothing. Draft keeps fixed spacing (decided, section 10);
what it gains is the spacing fade of section 6 and the depth behaviour of
section 6.1.

**Coin-side fallback.** Both clients use the one node in both modes, so
neither keeps a second path.

## 9. Phasing

1. `GridLevels` + `SoFCGrid` with the Coin-only draw. Gate: the sketch
   grid and the Draft grid look as they do today in mode 0.
2. The mode-3 capture companion and the backend generator, with the depth
   modes of section 6.1. Gates: pixel comparison with mode 0 at a few
   zooms (expect differences only where the fades and the depth stack
   apply), a rotation across the behind-geometry threshold, and a count
   test -- 20 zoom steps in a sketch edit with the scene server up publish
   **0** times (the session-93 probe reads 13 for one `fitAll` today).
3. Wire format and the browser viewer: the viewer at a different zoom from
   the desktop draws its own spacing; a desktop zoom does not republish.
4. Migrate `ViewProviderGridExtension`, with snap-parity tests:
   `getGridSize()` equals the step the backend drew for the same camera,
   and a browser client's snap uses its mirror's camera, not the desktop's
   (a desktop and a client at zooms one level apart snap to different
   steps).
5. Migrate `gridTracker`.
6. Optional: a floor grid in every 3D view as a view option, the first
   client that is not a workbench.

## 10. Decisions (2026-09-24)

1. **Draft stays fixed for now.** No automatic levels in Draft. The
   demand was searched: the one request on record is a comment on the
   Sketcher's adaptive-grid issue #7223 asking that the adaptive grid be
   general "so the same adaptive grid could be used in Draft WB", which its
   author then struck through on finding it already lived in Part. No Draft
   or BIM issue asks for it. The node supports it (`autoLevels`), so it is a
   preference away if that changes; Draft still gets the fades.
2. **Snaps use the viewer's camera** -- the grid on the screen the pointer
   is on (section 4.1).
3. **Perspective as Blender does it:** one fractional level from the
   camera distance, three levels drawn, fixed line counts, far-clip and
   grazing-angle fades; no distance bands (sections 2, 6).
4. **Depth as Blender does it:** the four-pass soft depth stack in general,
   behind geometry when looking straight down the normal in ortho
   (section 6.1).
5. **Upstream conflicts are accepted.** `ViewProviderGridExtension` and
   `gridTracker` diverge from upstream, and each sync of either file will
   need a hand merge.

Still open, to be settled while building rather than beforehand: the ortho
"straight down" angle tolerance, and how the behind-geometry depth is
expressed (a bias value or a depth-state clamp).

## Sources

- Blender issue #130464, "Overlay: Speed-up grid drawing":
  https://projects.blender.org/blender/blender/issues/130464
- Blender PR #148733, "Overlay: infinite grid using line draw":
  https://projects.blender.org/blender/blender/pulls/148733
- Blender `overlay_grid_vert.glsl` / `overlay_grid_frag.glsl` (current main):
  https://github.com/blender/blender/tree/main/source/blender/draw/engines/overlay/shaders
- Blender `overlay_grid.hh` and `overlay_shader_shared.hh` (current main):
  https://github.com/blender/blender/tree/main/source/blender/draw/engines/overlay
- FreeCAD issue #7223, "[Feature Request] Adaptive sketcher grid":
  https://github.com/FreeCAD/FreeCAD/issues/7223
- FreeCAD PR #7754, "Sketcher: Grid tool + automatic resize of grid":
  https://github.com/FreeCAD/FreeCAD/pull/7754
- FreeCAD issue #13090, "Setting the Draft grid can be confusing":
  https://github.com/FreeCAD/FreeCAD/issues/13090
- Ben Golus, "The Best Darn Grid Shader (Yet)":
  https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
