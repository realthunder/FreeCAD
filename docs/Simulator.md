# Simulator -- physics for Assembly and for the renderer

Early findings and direction for adding real physics simulation to this
fork: rigid-body dynamics for the Assembly workbench, and physically
driven fluids for the bgfx renderer.

Status: findings + direction from a discussion 2026-09-07. No code, no
build work, no dependency added. Related: `docs/RoadMap.md` (the
workstreams this slots into), `docs/ComputeBoundaries.md` (the process
boundaries this adds a third one to), `docs/RenderEngine.md` (the bgfx
engine and its particle system), `docs/ExternalRenderer.md` (the same
"adopt a mature engine rather than write one" posture, applied to
renderers).


## 1. Two questions that turned out to be one

Two separate asks opened this:

- **Motion and physics simulation in Assembly** -- parts that fall, that
  are driven by forces rather than by prescribed motions, that stop when
  they hit each other.
- **Physically driven fluids in the renderer** -- the `water`,
  `fountain`, `waterjet` and `rain` effects behaving like fluid rather
  than like scripted particles.

They look unrelated. They are not: both need a solver that this process
does not have and should not host, and both end at the same process
boundary. That convergence is the main conclusion of this document.


## 2. Finding: Assembly has no dynamics, and Ondsel does not supply it

The Assembly workbench vendors `src/3rdParty/OndselSolver` (17 MB, 658
files, 320 `.cpp`). A glance at the class list suggests a full multibody
dynamics engine: `ASMTContact`, `ASMTConeConeContact`,
`ASMTCylCylContact`, `ASMTForceTorque`, `ASMTPrincipalMassMarker`,
`MomentOfInertiaSolver`, and a family of integrators and Newton-Raphson
solvers.

**That reading is wrong, and it is worth recording why.** What is
actually wired, and what the solver actually implements:

- The only simulation entry point called from FreeCAD is
  **`runKINEMATIC()`** (`AssemblyObject.cpp:317`), alongside
  `runPreDrag` / `runDragStep` / `runPostDrag` for interactive dragging.
- Of the ~33 `run*` entry points the solver exposes, `src/Mod/Assembly`
  references **no** dynamics API at all: no `ASMTContact`, no
  `ASMTForceTorque`, no `ConeConeContact`, no `CylCylContact`.
- `ASMTConstantGravity.h` is included at `AssemblyObject.cpp:82` and
  **never instantiated**. A vestigial include.
- `makeMbdPart` is called as `makeMbdPart(str, plc)`
  (`AssemblyObject.cpp:1892`), so mass takes its default of **1.0**, and
  the body then hardcodes `setDensity(1.0)` and
  **`setMomentOfInertias(1.0, 1.0, 1.0)`** (`AssemblyObject.cpp:1936`).
  Every part in every assembly is unit mass with a unit inertia tensor.
- In the solver itself, the collision path is declared and empty:

      void SystemSolver::preCollision()             {}
      void SystemSolver::runCollisionDerivativeIC() {}
      void SystemSolver::runBasicCollision()        {}

  `runAllIC` contains a `"REBOUND"` discontinuity branch that calls all
  three, so the scaffolding is laid out with nothing behind it.
- The word `Dynamic` appears **zero times** in the solver's headers.

So OndselSolver as vendored is a **kinematics and assembly-constraint
solver**. The contact classes are geometry/data declarations with no
solve behind them; `MomentOfInertiaSolver` computes an inertia that
nothing consumes. Gravity, forces, contact, friction and restitution are
genuinely absent from this fork.

The placeholder mass properties are the tell: `(1, 1, 1)` is harmless
precisely because `runKINEMATIC` never reads it.

**Lesson worth keeping: do not infer capability from class names.** This
document's first draft asserted that the fork already had a dynamics
solver, on the strength of the file listing alone. Reading three
function bodies reversed the recommendation.


## 3. Finding: the renderer cannot host an SPH solver as it stands

`src/Gui/Renderer/bgfx/shaders/` holds **129 shaders, and not one is a
compute shader**: 63 `fs_`, 34 `fc_`, 30 `vs_`, plus `varying.def.sc`
and `compile.sh`. There is no `cs_` prefix anywhere, and no
`BUFFER_RW`, `IMAGE2D_` or `NUM_THREADS` in any source. The engine is
a pure raster pipeline.

The stateful particle system (`BGFXViewParticles.cpp`, 397 lines) keeps
particle state in ping-pong **textures** laid out `gridW x gridH`, and
integrates them in fragment shaders; `splatImpacts` writes impact
ripples onto a heightfield. The "grid" there is a state-texture layout,
not a spatial acceleration structure.

SPH needs neighbour search -- a spatial hash, built and queried with
atomics -- which in practice means compute shaders. **WebGL2 has no
compute shaders at all.** So an in-renderer GPU fluid solver cannot be
made to run on the browser tier by any amount of care; it is not a
porting problem, it is a missing capability in the target API.

That is the constraint that forces the decision in section 4.


## 4. Ruling: two fidelity modes, accuracy first

Ruled 2026-09-07. This settles what was previously the first open
question in section 9: **both products are wanted, and for CAD the
accurate one comes first.**

- **Accurate mode is the default.** Physically accurate simulation, run
  offline at whatever timestep and resolution correctness needs. Its
  output is a trajectory, which is recorded and played back. This is the
  mode a CAD user is asking for when they ask whether the part falls
  over.
- **Real-time mode is an option, reached by relaxing accuracy.**
  Interactive rates, GPU, the fidelity the existing procedural effects
  already have.

**The consequence that changes the architecture: record-and-play is a
first-class desktop feature, not a browser fallback.** An accurate
simulation cannot be watched live at interactive rates on *any* tier,
the desktop included -- it is computed, then played. So playback is the
normal way a user sees an accurate result on their own machine, and the
browser tier is simply another consumer of the same recording rather
than a degraded special case.

Three things follow:

- The **trajectory is the primary artifact** of the simulator, not a
  streaming convenience. Its format, storage, versioning and size are
  core concerns rather than afterthoughts.
- The existing procedural `water`, `fountain`, `waterjet` and `rain`
  effects are **not superseded**. They become the real-time mode of the
  same feature, which is a better outcome than either replacing them or
  leaving them as a parallel unrelated system.
- One effect, two modes, one appearance target: the relaxed mode should
  read as a cheaper version of the accurate one, not as a different
  effect. That is a design constraint on both.

The second half of the ruling stands as before. `CLAUDE.md` carries a
standing preference to keep browser and mobile portability open and to
avoid desktop-only assumptions in renderer work. **That default is
deliberately set aside for this workstream.** Fluid simulation targets
the **desktop tier first**, free to use compute shaders, GPU buffers and
whatever else the desktop backends offer. The browser and mobile tiers
receive **results, streamed** -- they do not run the solver.

Why that is defensible rather than a shortcut:

- The streaming tier already exists and already carries per-frame scene
  updates (`SceneServer`, `SceneDump`, `SceneLadder`). Particle
  positions are small compared with the geometry it already ships.
- It matches the split the fork already made for headless serving: the
  heavy side runs where the capability is, the thin client renders what
  it is handed.
- The alternative -- constraining the solver to what WebGL2 can express
  -- means either no accurate simulation at all, or a fragment-shader
  contortion that is slower and less accurate on *every* tier including
  the desktop, to serve the weakest one.
- And with accuracy first, the browser could not have run the accurate
  mode anyway. Streaming is not a concession made to the browser; it is
  how every tier consumes an accurate result.

What this costs, and what to watch:

- **Two modes of one effect can drift apart.** The accurate result and
  the real-time approximation must stay recognisably the same
  phenomenon. Worth a visual comparison harness early, not late.
- **Authoring must not become desktop-only.** If an effect can only be
  set up on the desktop tier, the browser tier stops being a client of
  the same document and becomes a viewer of a recording. Setting up a
  simulation and running one are different privileges.
- **Playback is not interaction, on any tier.** Accepted deliberately in
  accurate mode -- it is what accuracy costs. Real-time mode exists
  precisely for the cases where interaction matters more than fidelity.


## 5. Chrono compared with MBDyn

The two candidate engines, compared on what matters here.

| | Project Chrono | MBDyn |
| --- | --- | --- |
| License | **BSD-3-Clause** | **GPL-2.1** |
| Origin | UW-Madison (SBEL) | Politecnico di Milano, aerospace |
| Language / build | C++, CMake | C++, autotools |
| Current version | 10.0.0 | actively developed |
| Rigid bodies | yes | yes |
| General contact / collision | **yes**, real collision detection + friction | **no** general contact |
| Flexible bodies | FEA module, co-rotational elements | **geometrically exact beams and shells, CMS** -- its strength |
| Fluids | **FSI: SPH and TDPF**; granular DEM | **hydraulic networks only** (1D lumped) |
| Parallel / GPU | multicore, GPU DEM, MPI (synchrono) | RTAI real-time; no GPU |
| CAD import | `chrono_cascade` (OCCT) | none |
| Bindings | SWIG Python (PyChrono) and C# | input file; C/C++/Python co-simulation API |
| Packaging | **conda-forge `chrono` and `pychrono` 10.0.0** on linux-64, linux-aarch64, linux-ppc64le, osx-64, osx-arm64, win-64 | distro packages, Linux-centric |
| Integration mode | link **or** separate process | separate process only |

Five points decide it:

1. **Fluids.** Only Chrono has any. MBDyn's "hydraulic networks" are 1D
   lumped pressure-flow elements -- pipes, orifices, accumulators -- not
   free-surface fluid. For the renderer question MBDyn is out on
   capability alone, not on preference.
2. **Contact.** Chrono does real collision detection with friction.
   MBDyn does not do general contact. For Assembly, "parts stop when
   they hit each other" *is* the feature, so this is close to decisive
   on its own.
3. **License.** BSD-3 leaves the in-process option open if it is ever
   wanted. GPL-2.1 means MBDyn can only ever be a separate process --
   which is what the existing FreeCAD MBDyn workbenches do, so it is not
   fatal -- but it permanently forecloses a choice and puts a copyleft
   boundary into a distribution story that ships conda packages.
4. **Packaging.** `chrono` is already built on conda-forge for every
   platform this fork ships. Given the effort this project already
   spends on nine feedstocks, an engine that needs none is worth real
   weight.
5. **Where MBDyn genuinely wins.** Geometrically exact beams and shells,
   component mode synthesis, and rotorcraft/fixed-wing aeroelasticity.
   If the goal were flexible-structure or rotor dynamics, MBDyn would be
   the better engine and it would not be close. That is not the goal
   here.

**Recommendation: Chrono**, on fluids, contact and license. MBDyn stays
noted as the reference for flexible-body and aeroelastic work this fork
is not doing.

Scope note: only these two were compared, because only these two were
asked for. Bullet, PhysX, and Exudyn were not evaluated and may deserve
a look before anything is committed -- particularly if fluids were
dropped from scope, which would remove Chrono's biggest advantage.


## 6. What not to take from Chrono

Adopting Chrono does not mean adopting all of it.

- **`chrono_cascade` -- do not link it; read it.** It pins **OCCT 7.9.2
  / 7.9.3, and the project states other versions are not compatible**;
  this fork is on 8.0.1. The conda build also excludes cascade (along
  with vsg3d and ROS), so using it would mean building Chrono ourselves
  and giving up the packaging advantage that is one of the reasons to
  choose it. And we do not need it: FreeCAD already gets volume, centre
  of mass and the inertia tensor from OCCT's `BRepGProp`, and already
  tessellates through `TShapeRenderCache`. `chrono_cascade` exists for
  hosts that have no CAD kernel in hand.

  It is still worth reading as a reference for the bridge we would
  write: `ChCascadeDoc` walks an OCAF document and pulls volume
  properties off a shape, and `ChBodyEasyCascade` / `ChCascadeBodyEasy`
  set mass and inertia from geometry and displace the centre of gravity
  via `ChBodyAuxRef`. Eight files.
- **`chrono_sensor`** -- ray-traced camera and LiDAR. Overlaps our
  renderer and Cycles. Skip.
- **`chrono_irrlicht`, `chrono_vsg`** -- runtime visualisation. We have
  a renderer.
- **`chrono_vehicle`, `chrono_ros`** -- off-topic for CAD.


## 7. Where the boundary goes

`docs/ComputeBoundaries.md` section 1 defines two process boundaries and
insists they not be conflated. The simulator is a **third**, and it
behaves unlike either:

- **Boundary A** (GUI client to headless document server) carries
  *semantic operations* -- create feature, set property, commit.
- **Boundary B** (intra-server parallel workers) carries *finished
  result shapes* on DAG edges, once per recompute.
- **Boundary C, the simulator** carries a **time series**. It is
  configured once from document state, then runs and emits state per
  timestep for as long as the simulation lasts. Neither request/response
  nor bulk-once fits it.

Consequences worth stating before anything is built:

- The simulator is **fed from** the document (bodies, joints, masses,
  collision geometry) but is not part of the recompute DAG. A recompute
  invalidates a simulation; it does not participate in one.
- Its output is a **trajectory**, not a shape: per-timestep placements
  for rigid bodies, per-timestep particle positions for fluids. That is
  what gets stored, replayed, and streamed to thin clients.
- Because the output is a trajectory, **desktop playback and browser
  streaming are the same mechanism**, differing only in where the
  recording is read. The desktop is the primary consumer (section 4),
  not an exception to the streaming path.
- Crash isolation comes along with it, which matters for a contact
  solver being fed CAD-quality geometry.


## 8. Staging

Ordered so that each stage is useful even if the next never happens.

- **Stage 0 -- real mass properties.** Replace the `(1.0, 1.0, 1.0)`
  placeholders in `makeMbdPart` with values from `BRepGProp`: mass from
  density and volume, the real inertia tensor, the real centre of mass.
  No new dependency, no engine decision, and correct on its own terms.
  **No dynamics engine of any kind is usable until these numbers are
  real**, so this is the prerequisite whichever way section 5 goes.
- **Stage 1 -- offline spike, and the accuracy questions.** Drive Chrono
  standalone from a FreeCAD assembly exported by hand. Answers what must
  be known before any integration: does CAD-derived collision geometry
  behave, how bad is tessellation-based contact, what timestep is
  needed. **Settle the unit system here** (section 9) -- with accuracy
  as the primary product, a silent mm-versus-m mismatch is not a bug to
  find later, it is the whole result being wrong by orders of magnitude.
- **Stage 1b -- validation corpus.** Cases with known answers --
  pendulum, projectile, restitution, and a dam-break for the fluid side
  -- run as tests. "Physically accurate" is a claim that has to be
  demonstrated and then defended against regression. The FEM port built
  exactly such a corpus and it paid for itself.
- **Stage 2 -- the boundary.** Define the Boundary C protocol and the
  trajectory format. Run the simulator as a process; no in-process
  linking even though BSD-3 permits it.
- **Stage 3 -- Assembly dynamics.** Gravity, forces, contact, driven by
  real masses from stage 0.
- **Stage 4 -- fluids, desktop, accurate.** Chrono FSI, computed
  offline, results into the renderer as instanced geometry or a state
  texture. Record and play, on the desktop, is what this stage delivers.
- **Stage 5 -- fluids, streamed.** The same recording to the browser
  tier over the existing scene stream. Cheap once stage 4 exists,
  because it is the same trajectory read from somewhere else.
- **Stage 6 -- real-time mode.** Reconcile the existing procedural
  effects with the accurate results as the relaxed mode of one feature,
  against the appearance target in section 4. Deliberately last: it is
  the mode we already have something for, and it is easier to
  approximate a result that exists than to guess at one that does not.


## 9. Open questions

- **Units.** Chrono is unit-agnostic and SI by convention (m, kg, s);
  FreeCAD works in mm by default. Feeding millimetre lengths to a solver
  configured with SI gravity is wrong by a factor of a thousand and
  produces plausible-looking motion, which is the dangerous kind of
  wrong. With accuracy as the primary product this has to be settled
  explicitly at the boundary, not left to convention on either side.
  The fork's own unit machinery is `src/Base/Unit.*` and
  `src/Base/Quantity.*`; there is no design doc for it to defer to.
- **Determinism.** CAD users expect the same input to give the same
  answer twice. Chrono's multicore and GPU solvers are not obviously
  bit-reproducible, and a stored trajectory that cannot be regenerated
  is awkward to defend. Decide whether reproducibility is a requirement
  or a nice-to-have before choosing solver backends.
- **How much do the two modes share?** One authoring surface producing
  both an accurate run and a real-time approximation is the goal stated
  in section 4, but emitters, boundaries and material parameters may not
  map cleanly between an SPH solver and a procedural effect. If they do
  not, "one effect, two modes" is aspiration rather than design.
- **Does Assembly dynamics replace or sit beside the kinematic solve?**
  Prescribed motions and force-driven motion are different modes, and
  users will want both.
- **Collision geometry from CAD.** Tessellated contact is the easy
  answer and is wrong at edges and small features. Convex decomposition
  is the usual remedy and is its own workstream.
- **Does the trajectory belong in the `.FCStd`?** Fluid trajectories are
  large. Probably a `FileBlobs` matter -- see
  `docs/FileBlobsManager.md`.
- **Is Ondsel kept?** If Chrono lands for dynamics, the kinematic solver
  still has a job (assembly constraint solving and dragging), so the
  answer is probably yes -- but it should be a decision, not a drift.
