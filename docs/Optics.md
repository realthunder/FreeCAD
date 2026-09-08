# Optics -- can this fork carry a serious optics workbench?

An expansion of `docs/Simulator.md`. That document opened a third process
boundary for long-running solvers that are fed by the document but are not
part of the recompute DAG. Optics turns out to be a second tenant of that
boundary, and a much better fit for this fork than the fluid half of the
simulator work.

Status: findings, a library survey with licenses verified against primary
sources, a recommendation, and staging. Discussion of 2026-09-08. No code,
no build work, no dependency added. Related: `docs/Simulator.md` (Boundary
C, the two-fidelity ruling, the units question this shares),
`docs/RenderEngine.md` (the engine most of this would be built on),
`docs/MaterialStorage.md` (where optical materials belong),
`docs/ExternalRenderer.md` (the "adopt a mature engine" posture),
`docs/RoadMap.md`.


## 1. "Optics" is four products, and only one of them wants CAD

The word covers four disciplines that use different mathematics, different
software, and are wanted by different people. Conflating them is how these
projects fail.

| | What it is | State of open source |
| --- | --- | --- |
| **A. Sequential imaging design** | Rays through an *ordered* list of analytic surfaces. Paraxial layout, aberrations, spot diagrams, MTF, merit-function optimisation, tolerancing. Zemax OpticStudio, CODE V. | **Well served, free.** Optiland (MIT) and rayoptics (BSD-3) are both real. |
| **B. Non-sequential illumination and stray light** | Rays through *whatever they hit*, in a real 3D assembly. Photometric sources, scattering, absolute irradiance on detectors, ghost and stray-light path analysis. TracePro, LightTools, Speos. | **Not served at all.** Every option is commercial or a physics-lab special case. |
| **C. Physical / wave optics** | Diffraction, PSF, coherence, polarization, Gaussian beams. | **Well served, free, permissive.** POPPY, prysm, HCIPy, LightPipes, diffractio. |
| **D. Nanophotonics / EM** | Sub-wavelength structures, FDTD, mode solvers. | **Served, but GPL.** Meep, openEMS. |

The asymmetry in the right-hand column is the whole argument.

**Domain A does not want CAD.** A lens prescription is an ordered list of
analytic surfaces with a stop and an image plane. There is no assembly, no
mechanical context, no solid. Wrapping a free Python lens designer in a
FreeCAD workbench adds convenience and no capability, and nobody changes
tools for convenience.

**Domain B is a solid-modelling problem that happens to be about light.**
It is rays inside a barrel, past a baffle, off an anodised mount, through
a lens that is a real solid with a ground edge, onto a sensor that is a
real part. The commercial tools import STEP badly because they are optics
programs that grew a geometry kernel. FreeCAD *is* the geometry kernel.
That inversion is the opportunity, and it is the only place in this survey
where a CAD program can offer something that does not already exist.

So the question in the title has a sharper form: **can this fork build a
serious non-sequential illumination and stray-light tool?** Sections 2
through 4 say what stands in the way; sections 6 through 8 say what to
adopt for the other three domains rather than write.


## 2. Finding: most of a non-sequential engine is already vendored here

This fork is closer to a domain-B engine than it looks, because the
renderer workstream has already paid for the expensive parts.

Present in the tree today:

- **`src/3rdParty/cycles`** -- Cycles, Apache-2.0. A production Monte
  Carlo light-transport engine, vendored and built.
- **Embree is on.** `src/3rdParty/CMakeLists.txt:174` forces
  `WITH_CYCLES_EMBREE ON`, and `embree 4.4.1` is already installed in
  `.conda/freecad`. Ray-scene traversal, the part everyone rewrites badly,
  is done.
- **`src/3rdParty/cycles/src/util/ies.cpp`** -- an IES LM-63 photometric
  file parser, already there. Real luminaire data is the entry ticket for
  illumination work and we already have the reader.
- **Thin-film interference and dispersion** in
  `src/kernel/closure/bsdf_util.h` and `bsdf_microfacet.h`. Not a coating
  designer, but the physics is not absent.
- **MNEE** (manifold next-event estimation) -- specifically the machinery
  for finding light paths *through* refractive surfaces. Caustics are what
  an illumination tool computes all day.
- **A MaterialX bridge and a shader graph editor**
  (`CyclesMaterialX.cpp`, `Renderer/GraphEditor/`). A scatter model is a
  node in a graph we already author and already evaluate.
- **Per-face materials.** The per-face appearance and per-face texture
  work means a face already carries its own material. A lens element is
  one solid with two optical faces and a ground edge; that only expresses
  cleanly per-face, and the mechanism exists.
- **Material cards and blob storage** (`docs/MaterialStorage.md`,
  `docs/FileBlobsManager.md`). A glass, a coating stack and a scatter
  model are card types, not a new storage system.
- **A streaming display tier.** `SceneServer` already ships derived data
  to the browser.

What is genuinely missing, stated precisely so it is not underestimated:

- **Spectral transport.** Cycles is RGB. `src/kernel/svm/wavelength.h`
  maps a wavelength *to a colour for shading*; it does not carry a
  wavelength on the ray. Chromatic aberration, coating response as a
  function of angle and wavelength, and dispersion as an engineering
  answer rather than a rainbow all need a wavelength per ray.
- **Absolute radiometry.** A renderer's units are arbitrary; it only has
  to be self-consistent up to the tone map. Watts, lumens and lux *are*
  the product here.
- **Detectors.** Cycles has film and render passes. It has no notion of
  "irradiance on that face, in W/m2, with a Monte Carlo standard error".
- **Path tagging.** "Which surface sequence produced this ghost" is the
  entirety of stray-light work. No renderer records it, because no
  renderer needs it.
- **Exact surfaces.** Section 3. This is the hard one.


## 3. Finding: tessellation is the line between a toy and an instrument

Every open-source optics tool that traces against CAD geometry traces
against triangles, and that is why none of them is used for engineering.
The argument is worth writing out, because it also sets the shape of the
fix.

Take a spherical surface of radius R meshed with a maximum linear
deflection d (chord-to-arc sagitta). The half-angle theta subtended by one
facet satisfies d = R(1 - cos theta), so theta ~ sqrt(2d/R). The facet
normal is constant across the facet while the true normal sweeps 2*theta,
so the **normal error is of order theta**.

For a refracting surface with n = 1.5 near normal incidence, a normal
error delta perturbs the transmitted ray by roughly delta*(1 - 1/n),
about delta/3.

With R = 50 mm and a not-unreasonable d = 0.05 mm:

    theta ~ sqrt(2 * 0.05 / 50)  = 0.045 rad = 2.6 deg
    ray error ~ 0.045 / 3        = 0.015 rad
    transverse error at 50 mm    ~ 0.75 mm

A diffraction-limited spot is a few micrometres. The mesh is wrong by two
to three orders of magnitude.

Now tighten the mesh a hundredfold, d = 0.0005 mm:

    theta ~ 0.0045 rad,  ray error ~ 0.0015 rad,  ~75 um at 50 mm

Still twenty times too large -- and that mesh has roughly a hundred times
the triangles. **The error falls as sqrt(d).** You cannot mesh your way to
an instrument; you can only mesh your way to a slower toy. That square
root is the finding.

The remedy is not a finer mesh, it is a different intersection:

- **Analytic optical primitives, traced exactly** -- sphere, conic,
  even-asphere, toroid, cylinder, and Zernike or XY-polynomial freeform.
  This is what commercial non-sequential codes do, and OCCT already
  *describes* the common cases (`Geom_SphericalSurface`,
  `Geom_ToroidalSurface`, `Geom_CylindricalSurface`), so the surface type
  is already in the document and only the intersector is missing.
- **Newton refinement onto the exact OCCT surface** for anything else,
  including B-spline freeforms: use the triangle hit as the seed, then
  converge onto the true surface. Costs a projection per hit, works for
  every surface OCCT can carry.

And a split that makes the whole thing tractable: **the exactness
requirement is specific to specular and refractive optical surfaces.** The
mechanical surroundings -- barrels, baffles, black anodised interiors --
scatter diffusely, and a 2.6 degree normal error on a Lambertian surface
changes nothing. So the engine is deliberately hybrid: exact primitives
for the optics, ordinary tessellation for the mechanics. That is also what
keeps it fast, and it is the reason the two-kinds-of-geometry design is a
feature rather than a compromise.


## 4. Finding: the data problem is already solved, which is unusual

Optical simulation is normally blocked on data before it is blocked on
physics: you cannot trace a glass whose dispersion you may not ship.

**refractiveindex.info releases its database under CC0 1.0** -- public
domain dedication, explicitly "free to use, modify, and distribute its
content without any restrictions, even for commercial purposes, no
permission required". It carries the Schott, Ohara, Hoya, CDGM, Hikari,
Sumita and LZOS catalogs, plus metals and coating dielectrics.

That means the fork can **ship a built-in glass catalog**, which almost
nothing else in this space can.

The contrast is instructive. `opticalglass` (BSD-3, by the rayoptics
author) reads six vendor catalogs but bundles none of them -- "all rights
and ownership of the data is retained by the original owners". That is the
correct and defensible split, and it is the one to copy:

- **Ship** the CC0 database as the default catalog.
- **Read** vendor `.agf` / Zemax catalogs from the user's own copy, never
  redistribute them.

Coatings are the honest exception. Real vendor coating recipes are
proprietary and will not become available. A serious tool therefore models
a *user-specified* n,k layer stack via the standard matrix method, and
offers idealised stand-ins ("ideal AR, R = 0.25% over 450-650 nm") for the
common case where the user knows the specification but not the recipe.
Saying so up front is better than implying a coating library that cannot
exist.


## 5. Prior art inside FreeCAD, and where it stops

Two addons already exist, and both are worth reading before writing
anything.

- **`chbergmann/OpticsWorkbench`** (LGPL-3, pure Python). Geometrical
  optics on FreeCAD objects: mirrors, absorbers, lenses, 2D from
  sketches, Sellmeier dispersion, energy-density plots on absorbers, and
  a 1D grating model following Ludwig 1973 with multiple diffraction
  orders. Genuinely useful for layout and teaching.
- **`zaphB/freecad.optics_design_workbench`** (LGPL-3), built on the
  above. Adds ray-fan and Monte Carlo modes with results written to disk
  for Jupyter analysis, and **parameter optimisation driving FreeCAD model
  parameters** -- the example tunes a lens radius to minimise detector
  spot size.

Where they stop:

- A **Python ray loop is the ceiling.** Non-sequential work is millions of
  rays; this is a compiled-kernel problem.
- They **convert** an object into an optical object rather than annotating
  it, which puts optics beside the model instead of on it, and does not
  survive Link and assembly structure.
- No absolute radiometry, no coatings, no scatter models, no path tagging,
  and triangle-or-analytic intersection is not addressed as the issue
  section 3 says it is.

What to take from them: the **optimisation-over-CAD-parameters** idea is
right and is exactly what a CAD-hosted optics tool can do that Zemax
cannot -- the parameter space is the model, not a surface list.

(FreeCAD's own legacy `Raytracing` workbench was a POV-Ray / LuxRender
exporter, not optics, and is not in this tree.)


## 6. Library survey, licenses verified

Licenses below were read from the projects' own repositories or license
files, not inferred.

| Library | License | Language | Domain | Packaged | Note |
| --- | --- | --- | --- | --- | --- |
| **Optiland** | **MIT** | Python + PyTorch | A, B pre-release | conda-forge, PyPI | Differentiable, GPU, tolerancing, coatings. Non-sequential is in pre-release; polarization and wave optics are roadmap. Very active. |
| **rayoptics** | **BSD-3** | Python | A | conda-forge, PyPI | Mature. Imports Zemax `.zmx`/`.zar`, CODE V `.seq`. Paraxial layout, Qt GUI. |
| **opticalglass** | **BSD-3** | Python | glass data | PyPI | Reads CDGM/Hikari/Hoya/Ohara/Schott/Sumita and `.agf`; interfaces refractiveindex.info. Ships no vendor data. |
| **batoid** | **BSD-2** | C++ core, Python | A/B, telescopes | PyPI | Fast, survey-telescope oriented. Narrow but clean. |
| **prysm** | **MIT** | Python | C + sequential rays | PyPI | Phase retrieval, polynomials, segmented apertures. |
| **POPPY** | **BSD-3** | Python | C | PyPI | Fraunhofer/Fresnel propagation, JWST heritage. |
| **HCIPy** | **MIT** | Python | C | PyPI | Vector fields, polarization, AO, coronagraphs. |
| **LightPipes** | **BSD-3** | Python | C | PyPI | Coherent propagation through optical elements. |
| **diffractio** | **MIT** | Python | C | PyPI | Scalar and vector diffraction. |
| **Poke** | **BSD-3** | Python | C | PyPI | Polarization ray tracing, Gaussian beamlet decomposition, thin films. **Today it only bridges to Zemax and CODE V.** |
| **Mitsuba 3** | **BSD-3** | C++ / Dr.Jit | B physics | PyPI | Spectral *and* polarized *and* differentiable variants. Research-oriented. |
| **Cycles** | **Apache-2.0** | C++/CUDA/HIP | B kernel | **vendored here** | Embree, MNEE, IES, thin film, dispersion. RGB only, no polarization. |
| **Embree** | **Apache-2.0** | C++ | BVH | **in the env** | 4.4.1 installed. |
| **ROBAST** | **LGPL-2.1** | C++ (ROOT) | B | source | Real non-sequential tracer, but drags in the ROOT framework. |
| **Astree** | **LGPL** | C++ | A | source | Small, GUI-oriented. |
| **Goptical** | **GPL-3** | C++ | A + B | no | GNU project, status "alpha", effectively unmaintained since ~2012; several build-fix forks. |
| **pyoptools** | **GPL-3** | Python/Cython | A + B | PyPI | Active. |
| **Meep** | **GPL-2.0-or-later** | C++/Python | D | conda | FDTD, MPI, mature. |
| **Geant4** | custom permissive | C++ | B transport | source | Full optical-photon physics, but a particle-physics idiom and a heavy toolkit. |

Two observations from the table.

**The permissive column is where the good tools are.** Every library this
fork would actually want -- Optiland, rayoptics, opticalglass, the whole
wave-optics set, Mitsuba, Cycles, Embree -- is MIT, BSD or Apache. The GPL
entries (Goptical, pyoptools, Meep) are either unmaintained, narrower, or
a different discipline. Licensing is, unusually, not the constraint.

**The domain-B row is empty of anything adoptable.** ROBAST is the closest
real thing and it is a Cherenkov-telescope library on ROOT. The literature
says as much: surveying non-sequential tracers for telescope work, the
field notes that a standard open-source program that can be widely
disseminated is lacking. That is the gap.


## 7. License compatibility with this fork

The root `LICENSE` is the GNU Library GPL v2, and source headers say
"version 2 of the License, or (at your option) any later version" --
**LGPL-2.0-or-later**. That "or later" is load-bearing.

- **MIT, BSD-2, BSD-3, LGPL-2.1, LGPL-3** -- link freely, bundle freely.
  This covers Optiland, rayoptics, opticalglass, batoid, prysm, POPPY,
  HCIPy, LightPipes, diffractio, Poke, Mitsuba 3, ROBAST.
- **Apache-2.0** (Cycles, Embree) -- incompatible with GPL-2.0-*only*, but
  compatible with GPL-3 and LGPL-3. The "or later" permits taking LGPL-3,
  so it links. In practice the question is already settled: this fork
  vendors Cycles today.
- **GPL-2 / GPL-3** (Meep, pyoptools, Goptical) -- not linkable. Usable
  only as a separate executable across a process boundary. That is exactly
  the FEM workbench's relationship to CalculiX, and the relationship
  `docs/Simulator.md` notes for MBDyn, so there is precedent and it is not
  fatal. It does mean such an engine can only ever be an optional external
  tool, never core, and never a hard dependency.
- **Data.** CC0 refractiveindex.info can be shipped. Vendor `.agf`
  catalogs cannot; read them from the user's copy (section 4).

One trap worth naming because it is easy to wave away: **Python is not a
loophole.** A GPL-3 Python library imported by a workbench raises the same
derivative-work question as linking a shared library -- the FSF's position
is that import *is* linking. "It is only a Python addon" does not make
pyoptools or a Goptical binding safe to depend on. If a GPL engine is
wanted, it goes behind a process boundary like CalculiX, deliberately.


## 8. Recommendation

**Yes, a serious optics workbench is possible here, and its subject is
domain B.** Four parts:

1. **Do not write a lens designer.** Adopt **Optiland** (MIT) as the
   sequential engine -- it is differentiable, GPU-capable and actively
   developed -- and **rayoptics** (BSD-3) as the import path (`.zmx`,
   `.seq`) *and as a cross-check oracle*. Two independent implementations
   that agree on a published design is a cheap and genuine correctness
   claim, and this fork's FEM port established that habit already.
   **opticalglass** (BSD-3) is the glass layer over the shipped CC0
   catalog.
2. **Build the non-sequential engine**, because nobody has one, on the
   Monte Carlo kernel already in the tree, with the exact optical-surface
   layer of section 3 that no renderer has.
3. **Take physical optics from the permissive Python set** -- POPPY,
   prysm, HCIPy, LightPipes -- rather than writing propagation. And
   consider feeding **Poke** our ray data: it does polarization ray
   tracing and Gaussian beamlets but today speaks only to Zemax and CODE
   V, so an open ray source would be a contribution worth making upstream.
4. **Keep FDTD out.** Meep is GPL and a different discipline. If it ever
   lands it lands as an external process, like CalculiX.

### The one engine choice to settle: Cycles or Mitsuba 3

This is the real decision and it should be settled by a spike, not by
argument.

- **Cycles** is already here, already bridged to our materials and shader
  graph, already has Embree, MNEE and an IES parser, and is
  production-hardened by a decade of Blender. It is RGB and has no
  polarization -- both would have to be added to an engine that was not
  designed for them.
- **Mitsuba 3** is BSD-3 and is *already* spectral, *already* polarized
  and *already* differentiable; those are variant selections, not
  features to add. It is exactly the physics domain B needs, correct out
  of the box, and it has been used for optical-photon simulation in
  physics. It costs a second renderer stack (Dr.Jit, its own JIT and
  scene format), and it is research code rather than production code.

Note the structural echo with `docs/Simulator.md` section 4, which ruled
two fidelity modes for fluids with accuracy first. The natural resolution
here is the same shape: **Mitsuba as the accurate offline oracle, Cycles
and bgfx as the interactive preview** -- one feature, two modes, the
relaxed one reading as a cheaper version of the accurate one. If that
holds, the two workstreams share not only a boundary but a posture.


## 9. Where the boundary goes: optics is Boundary C too

`docs/Simulator.md` section 7 opened **Boundary C** for a solver that is
configured once from document state, runs long, and emits a derived
dataset that is not a shape and is not part of the recompute DAG. An
optics run is the same kind of thing with a different payload:

| | Simulator | Optics |
| --- | --- | --- |
| Configured from | bodies, joints, masses, collision geometry | surfaces, materials, coatings, sources, detectors |
| Emits | a trajectory: state per timestep | a result set: irradiance and illuminance maps per detector, spot and MTF and encircled-energy tables, path-tagged ghost inventories, ray bundles for display |
| Size | large | large |
| In the DAG | no; a recompute invalidates it | no; a recompute invalidates it |
| Consumed by | desktop playback, browser stream | desktop display, browser stream |

**Consequence worth stating before either is built: design Boundary C
once.** If the simulator invents a protocol for trajectories and optics
invents another for irradiance maps, the fork ends up with two
long-running-solver boundaries that differ for no reason. The right
abstraction is *a solver session configured from document state that emits
a versioned derived dataset*, with trajectory and optics result as two
payload types over one protocol.

### Units: the same open question, with more axes

The simulator's open question is millimetres versus metres against SI
gravity. Optics has the same class of bug on more axes:

- **Length** -- mm. The lens industry's convention and FreeCAD's default
  agree for once, which is luck, not design.
- **Wavelength** -- nm or um, and both conventions are in live use in the
  catalogs we would read.
- **Flux** -- W (radiometric) or lm (photometric).
- **Irradiance** -- W/m2 or lux.

And the radiometric-to-photometric conversion is **a V(lambda) weighting,
not a scale factor**, so it cannot be deferred to a display-time
multiply -- it has to happen inside a spectral integral. A tool that
reports lux from an RGB render is reporting a number that does not mean
what its label says.

One unit contract, written down once, shared by both solvers. Section 11
makes it stage 0 for exactly the reason the simulator doc gives: with
accuracy as the product, a silent unit mismatch is not a bug found later,
it is the whole result being wrong in a plausible-looking way.


## 10. What it looks like in this fork's idiom

The reuse inventory. This is where the renderer and material investment
pays, and it is most of the argument that this fork specifically can do
this.

- **Optical properties as an extension, not a parallel object graph.** A
  `DocumentObjectExtension` that annotates any Part feature, instead of
  the addons' pattern of converting an object into an "Optical Lens".
  Keeps `App::Link`, keeps assemblies, keeps the DAG, keeps the geometry
  editable by the normal tools.
- **Per-face assignment already exists.** A lens element is one solid with
  two optical faces and a ground edge; a mirror is one face of a mount.
  Only per-face expresses that, and the per-face appearance and per-face
  texture arcs already built the mechanism.
- **Material cards already exist.** A glass, a coating stack and a scatter
  model are card types under `docs/MaterialStorage.md`, stored in the
  document's `FileSet` like every other card. No new storage design.
- **The shader graph already exists.** A scatter model -- ABg,
  Harvey-Shack, or a measured BSDF table -- is a node in the graph the
  fork already authors and already compiles for Cycles.
- **Result display is renderer work already done.** A false-colour
  irradiance map on a detector face is a texture; a ray bundle is
  instanced geometry; the bgfx renderer draws both well already.
- **Streaming already exists.** The browser tier consumes an optics result
  exactly as it consumes a simulation recording.
- **Optimisation over the CAD parameter space** -- zaphB's good idea --
  fits this fork particularly well: the expression engine and the property
  system *are* the parameter space, and Optiland's PyTorch backend already
  produces gradients. The version worth building is a merit function over
  an **assembly**, not over a surface list, because that is the thing
  Zemax structurally cannot do.


## 11. Staging

Ordered so each stage is useful even if the next never happens -- the same
discipline `docs/Simulator.md` section 8 uses.

- **Stage 0 -- the unit and radiometry contract.** Length, wavelength,
  flux, irradiance, the V(lambda) weighting, and how each crosses Boundary
  C. Written down, shared with the simulator, before any code. Costs
  nothing; deferring it is the failure mode section 9 describes.
- **Stage 1 -- glass and coating data.** Ship the CC0
  refractiveindex.info catalog; read user-supplied `.agf`; implement the
  dispersion formula families (Sellmeier 1-3, Schott, Conrady,
  Herzberger, extended). Verify n_d and V_d against published values for
  a dozen well-known glasses. **Useful alone** -- it improves the two
  existing addons too, and it is a contribution even if the workbench
  never ships.
- **Stage 2 -- sequential design, adopted rather than written.** Wire in
  Optiland and rayoptics; import `.zmx`; reproduce a published Double
  Gauss spot diagram in both and against each other. Delivers a real lens
  designer early and cheaply, and establishes the cross-check habit before
  it is needed for something harder.
- **Stage 3 -- the exact optical surface.** The piece nothing else has:
  analytic intersection for sphere, conic, even-asphere, toroid and
  cylinder, plus Newton refinement onto a general OCCT surface. Benchmark
  against the tessellated answer so section 3's estimate becomes a
  measured number. **This stage decides whether the whole thing is an
  instrument**, so it comes before the engine choice depends on it.
- **Stage 4 -- the non-sequential spike: Cycles versus Mitsuba.** One
  honest problem -- a lens assembly in a barrel with a baffle, an extended
  source, a detector, and a ghost to find. Answers what spectral transport
  costs, whether polarization is needed, whether path tagging can be
  threaded through the host engine, and how much of that engine survives
  contact with absolute radiometry.
- **Stage 5 -- the boundary.** Boundary C as a shared design with the
  simulator: a solver process, a versioned result payload, storage via
  `FileBlobs`, streaming to the thin client. Crash isolation earns its
  keep here -- a tracer fed CAD-quality geometry finds every degenerate
  face in the model.
- **Stage 6 -- sources and detectors.** IES and LDT (Cycles already parses
  IES), LED and blackbody spectra, laser Gaussian, sun and sky. Detectors
  reporting irradiance, illuminance and angular distribution, **with a
  Monte Carlo standard error on every number** -- an illumination result
  without an error bar invites the user to read noise as structure.
- **Stage 7 -- analysis and display.** Spot diagrams, encircled energy,
  MTF, false-colour maps, ghost path inventory, ray display in the
  renderer.
- **Stage 8 -- physical optics.** Bridge to POPPY / prysm / HCIPy for PSF
  and diffraction; consider the Poke contribution from section 8.
- **Stage 9 -- optimisation over the CAD parameter space.**

A small item that sits outside this order and is worth doing whenever
convenient: **a prescription-to-solid feature** -- a parametric lens
element from radii, thickness, glass, conic and asphere terms. It is
small, obviously useful on its own, bridges domain A output into the
document, and would earn its place even if nothing else on this list
happens.


## 12. What "physically accurate" has to survive

A validation corpus, in the tradition of the FEM port and
`docs/Simulator.md` stage 1b -- and needed more here, because in optics
the claim gets audited by people who own a Zemax licence and will check.

- **Fresnel reflectance** at normal incidence and at Brewster's angle, s
  and p, against the closed form.
- **Prism at minimum deviation** with a known glass -- closed form, and it
  exercises dispersion.
- **On-axis irradiance from a Lambertian disc** -- closed form, and it
  exercises absolute radiometry.
- **Blackened integrating sphere** -- multiple-scatter throughput against
  theory. Catches energy bookkeeping errors that single-bounce tests miss.
- **Thin-film AR stack reflectance** versus a matrix-method reference.
- **Double Gauss spot diagram** versus the published prescription, and
  cross-checked between Optiland and rayoptics.
- **Airy pattern** from a circular aperture -- the wave-optics leg.
- **Energy conservation as an invariant on every run**: flux in equals
  flux absorbed plus flux escaped, within the Monte Carlo error. This one
  is the cheapest to implement and catches the most, and it should run on
  every simulation, not only in the test suite.


## 13. The honest case against

Stated plainly, because the case for is easy to over-sell.

- **The validation burden is heavier than any other workstream in this
  fork.** The FEM port had CalculiX to lean on; here we would *be* the
  reference implementation for domain B. In this field being confidently
  wrong is worse than being absent, and the users can tell.
- **Domain A is already free and already good.** A FreeCAD wrapper around
  Optiland is a nicety. It wins nobody.
- **Domain B is the valuable one and it is the hardest.** Exact surfaces,
  spectral transport, absolute radiometry, scatter models, path tagging,
  convergence estimates -- each is a subsystem, not a feature.
- **The data long tail never ends.** Coatings, measured scatter, vendor
  catalogs. A tool that cannot express the user's actual coating is a tool
  they cannot use for their actual job.
- **It competes with the renderer**, which `CLAUDE.md` names as the
  near-term focus. The mitigation is real but partial: it competes *with
  the same code*. Exact surfaces, spectral transport and absolute
  radiometry each make the renderer better, and stages 7 and 5 are
  renderer and streaming work outright.
- **It does not ride the simulator's dependency decision.** Chrono has no
  optics. This is a second engine choice, not a second use of the first --
  the sharing is the boundary and the posture, not the library.

And the case for, in one sentence: **there is no serious open-source
non-sequential illumination and stray-light tool, and this fork is closer
to having one than any other project is** -- because it already owns a
geometry kernel, a Monte Carlo transport engine with Embree under it, a
material and per-face system, a shader graph, and a streaming display
tier. That is not a small distance already covered.


## 14. Open questions

- **Cycles or Mitsuba 3** for the domain-B kernel. Settle by the stage-4
  spike, not by argument.
- **Is polarization in scope?** It decides the engine choice, and it is
  the difference between an illumination tool and an optical-engineering
  tool for anything with a beamsplitter, an LCD, or stress birefringence.
- **Do we track coherence?** Everything above is incoherent transport.
  Lasers, interferometers and speckle are a different product and probably
  belong to domain C entirely -- but that should be a decision.
- **Determinism.** Same input, same irradiance map? The simulator raises
  this; Monte Carlo sharpens it, because "the same to within the stated
  error" may be the honest contract rather than bit-identical, and that is
  a different promise to make to a CAD user.
- **Where does a result live** -- in the `.FCStd` or in `FileBlobs`? And
  are ray sets stored at all, or regenerated from a seed?
- **Does the workbench own document objects, or is it an extension plus a
  solver?** The extension answer is cleaner, but sources and detectors are
  genuinely new objects with no host to annotate.
- **Who is the first user?** A lighting designer, a lens designer, a
  stray-light analyst and a photonics researcher want four different
  programs. The staging above quietly answers "the illumination and
  stray-light analyst". That should be a decision, not a drift.
