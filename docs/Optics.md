# Optics -- can this fork carry a serious optics workbench?

An expansion of `docs/Simulator.md`. That document opened a third process
boundary for long-running solvers that are fed by the document but are not
part of the recompute DAG. Optics turns out to be a second tenant of that
boundary, and a much better fit for this fork than the fluid half of the
simulator work.

Status: findings, a library survey with licenses verified against primary
sources, a recommendation, and staging. Discussion of 2026-09-08, revised
the same day after the first draft's scoping was challenged and found
wrong -- section 1 records the correction rather than hiding it, because
the wrong axis it used is an easy one to reach for again. No code,
no build work, no dependency added. Related: `docs/Simulator.md` (Boundary
C, the two-fidelity ruling, the units question this shares),
`docs/RenderEngine.md` (the engine most of this would be built on),
`docs/MaterialStorage.md` (where optical materials belong),
`docs/ExternalRenderer.md` (the "adopt a mature engine" posture),
`docs/RoadMap.md`.


## 1. Correction: "wants CAD" is not the same axis as "has a library"

The first draft of this document sorted optics into four domains and
concluded that only non-sequential illumination wanted CAD. **That
conclusion was reached on the wrong axis and it is wrong.**

The error is precise and worth recording. The survey's right-hand column
asked *"does an open-source library exist"*, and the conclusion silently
treated that as *"is the workflow served"*. Those are different questions.
Optiland and rayoptics are excellent **libraries** for computing a lens
prescription. Neither is a workflow for laying out an experiment, and
neither can answer what happens to the beam when a mount drifts -- because
neither has ever heard of the mount.

The axis that actually matters is:

> **Does the answer depend on where the parts physically are, in three
> dimensions, in an assembly that someone also has to build?**

On that axis, sequential-versus-non-sequential is not the dividing line at
all. Sequential optics *in isolation* does not want CAD; sequential optics
*on a bench* wants it more than anything else in this document.

### Domain A splits three ways, and two of them are CAD-native

- **A1 -- prescription design.** "Give me a triplet that images at f/2.8
  over this field." The subject is an ordered list of analytic surfaces
  with a stop and an image plane. No assembly, no mechanical context, no
  solid. **Does not want CAD.** Optiland and rayoptics do this well and
  the original conclusion holds here, and only here.
- **A2 -- optical layout and experiment setup.** A laser, an AOM, a
  telescope, a waveplate, a fibre coupler, on a breadboard, in mounts, on
  posts. The beam path *is* an assembly, the components *are* parts with
  real geometry, and the thing being designed is simultaneously an optical
  system and a mechanical one. **Wants CAD.** This is what the user
  community around FreeCAD optics is actually doing, and PyOpticL
  (section 6) exists because of it, with published physics behind it.
- **A3 -- optomechanical tolerancing.** "What does 100 um of mount drift,
  or 0.5 degrees of post tilt, or a 20 K thermal excursion, do to my
  beam?" The perturbation lives in the **mechanical** model; its
  consequence is **optical**. **This wants CAD more than any other job in
  this document**, and it is the one nothing free can do.

A3 deserves its own paragraph because it is the strongest case and the
first draft missed it entirely.

A mirror whose mount tilts by delta deviates the reflected beam by 2*delta.
A half-milliradian of thermal drift in a kinematic mount -- unremarkable,
and roughly what a good mount actually does -- puts the beam a millimetre
off at one metre. Coupling that beam into single-mode fibre needs
micrometre-scale lateral accuracy. So a perturbation nobody would think to
model decides whether the system works at all, and the number that decides
it is determined by the **mechanical design**: where the pivot is, how
long the post is, what the mount is made of.

That this is a real and unmet need is not a guess. It is what Ansys built
NEST and the Speos optomechanical tolerancing tools for, and their own
documentation names the pain: mechanical pivot points sit away from the
optical axis and are determined by the mount, misalignments in one
assembly propagate through others, and setting this up in Zemax
historically meant hand-writing complex operand structures, error-prone
and slow. The commercial market has validated this job, at commercial
prices. Nothing free addresses it.

And it is a *parametric* question, which is what this program is. The
mount tilt is a property, the post length is a property, the expression
engine already relates them, and a tolerance study is a Monte Carlo over
those properties. A parametric modeller is the natural host for
optomechanical tolerancing in a way that an optics program with a STEP
importer structurally is not.

### Domain C splits four ways, and "waveguide" spans three of them

"Waveguide design and simulation" is three different stacks depending on
which waveguide is meant, so the word has to be disambiguated before it
can be scoped.

- **C1 -- integrated photonics (PIC).** Silicon photonics, GDSII layout,
  mode solvers, eigenmode expansion, FDTD. **The layout is a 2D + PDK
  problem and FreeCAD is not the natural host** -- `gdsfactory` (MIT) owns
  this and does it well. But the *cross-section* is three-dimensional, and
  **packaging is entirely ours**: fibre attach, submount, lid, thermal
  path, stress. That is a real role, and a narrow one. Do not try to
  become a layout tool.
- **C2 -- Gaussian beams and mode matching on a bench.** Beam waists, ABCD
  matrices, mode-matching into a cavity or a fibre, coupling efficiency.
  **Wants CAD**, because where the waist lands is decided by where the
  lenses sit on the breadboard. This is A2's inseparable companion --
  PyOpticL added Gaussian beam simulation for exactly this reason -- and
  it is the part of C that matters most here.
- **C3 -- hollow RF and millimetre-wave waveguide.** Metal solids, FDTD or
  FEM. **Wants CAD, and is already served**: EMStudioFree, the openEMS
  export plugins and the EM workbench already occupy this niche in
  FreeCAD (section 6). Do not duplicate it.
- **C4 -- aperture diffraction and PSF.** POPPY, prysm, HCIPy: the
  astronomy-flavoured wave optics. The pupil is a mask, not a solid.
  **Does not want CAD.** Adopt as a library; the original conclusion holds
  here.

### The corrected table

Sorted on the axis that matters.

| | Job | Wants CAD | Free option today |
| --- | --- | --- | --- |
| **A1** | Lens prescription design | no | **Optiland (MIT), rayoptics (BSD-3)** -- adopt |
| **A2** | Optical layout / experiment setup | **yes** | PyOpticL -- but **unlicensed** (section 6) |
| **A3** | Optomechanical tolerancing | **yes, most of all** | **nothing** |
| **B** | Non-sequential illumination, stray light | **yes** | **nothing** |
| **C1** | PIC layout and device sim | layout no, **packaging yes** | gdsfactory (MIT); solvers are GPL |
| **C2** | Gaussian beams, mode matching | **yes** | fragments only |
| **C3** | RF / mm-wave waveguide | yes | **already served in FreeCAD** -- don't duplicate |
| **C4** | Aperture diffraction, PSF | no | POPPY, prysm, HCIPy -- adopt |
| **D** | Nanophotonics FDTD | no (2D/3D grid) | Meep, MPB -- GPL, out of process |

Four rows are CAD-native and unserved or barely served: **A2, A3, B, C2**.
That is the subject of this document, and it is a good deal larger and
better-founded than the first draft's single row.


## 2. The finding that reorganises everything: four jobs, one engine

A2, A3, B and C2 look like four products. They are not. **All four consume
the same primitive**, and it is a primitive nothing in the open-source
world provides:

> a ray or beam traced through the *actual document geometry*, at the
> *actual placements*, against *exact surfaces*.

- **A2** needs it to route a beam path and place the next component where
  the beam actually goes.
- **A3** needs the same trace re-run under a perturbation of the
  mechanical parameters, thousands of times.
- **C2** needs it carrying a Gaussian q-parameter rather than a bare
  direction -- the same trace with a richer payload.
- **B** needs millions of them with wavelength, radiometry and scatter
  attached.

So this is **one core with four consumers**, not four workbenches. That
changes the shape of the work and improves it: the expensive, risky part
-- exact intersection against document geometry, section 4 -- is built
once and paid for four times, and the cheapest consumer (A2 routing) can
validate it long before the most expensive one (B) exists.

It also reorders the staging. The first draft put the exact-surface work
at stage 3, behind an adopt-a-lens-designer stage that was framed as a
warm-up. It is not a warm-up; it is the foundation, and A2 gives it a
payoff two stages earlier than B does. Section 12 is reordered
accordingly.


## 3. Finding: most of a non-sequential engine is already vendored here

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


## 4. Finding: tessellation is the line between a toy and an instrument

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

**And tolerancing is where this bites hardest.** Section 1 put a
half-milliradian mount drift at the centre of the A3 case. A 2.6 degree
facet normal error is 45 milliradians -- **ninety times larger than the
signal a tolerance study is trying to measure**. That is not "less
accurate", it is numerical noise swamping the answer, and it means
tessellated tracing cannot do optomechanical tolerancing at all, at any
mesh density that a sqrt law will reach. The most valuable job in section
1 is the one most tightly bound to the hardest finding here.

And a split that makes the whole thing tractable: **the exactness
requirement is specific to specular and refractive optical surfaces.** The
mechanical surroundings -- barrels, baffles, black anodised interiors --
scatter diffusely, and a 2.6 degree normal error on a Lambertian surface
changes nothing. So the engine is deliberately hybrid: exact primitives
for the optics, ordinary tessellation for the mechanics. That is also what
keeps it fast, and it is the reason the two-kinds-of-geometry design is a
feature rather than a compromise.


## 5. Finding: the data problem is already solved, which is unusual

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


## 6. Prior art inside FreeCAD, and where it stops

There is more of it than the first draft found, and the additions are the
ones that matter for A2, A3 and C3. FreeCAD is already, quietly, the host
of several optics and EM efforts. That is evidence for the corrected
thesis, not against it: people keep reaching for a CAD program for these
jobs because these jobs need one.

### Ray tracing on FreeCAD geometry

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
- **`cihologramas/freecad-pyoptools`** (**GPL-3**) brings the pyOpTools
  ray tracer into FreeCAD. So a compiled-kernel tracer inside FreeCAD does
  exist -- but GPL-3 means it can never be core here (section 8), only an
  optional addon.

Where they stop:

- A **Python ray loop is the ceiling.** Non-sequential work is millions of
  rays; this is a compiled-kernel problem.
- They **convert** an object into an optical object rather than annotating
  it, which puts optics beside the model instead of on it, and does not
  survive Link and assembly structure.
- No absolute radiometry, no coatings, no scatter models, no path tagging,
  and triangle-or-analytic intersection is not addressed as the issue
  section 4 says it is.

What to take from them: the **optimisation-over-CAD-parameters** idea is
right and is exactly what a CAD-hosted optics tool can do that Zemax
cannot -- the parameter space is the model, not a surface list.

(FreeCAD's own legacy `Raytracing` workbench was a POV-Ray / LuxRender
exporter, not optics, and is not in this tree.)

### PyOpticL -- the closest prior art for A2, and a licensing problem

**`UMassIonTrappers/PyOpticL`** is a code-to-CAD optical layout tool built
on FreeCAD, from the Niffenegger group at UMass. It places optics along
simulated beam paths with automatic and dynamic routing including branched
paths, generates modular baseplates meant to be machined or 3D printed and
bolted to an optical table, and as of v2.0 does Gaussian beam simulation.
It is published work -- Quantum, 2026-06-15 -- reporting over 99% fidelity
single-qubit gates in a trapped-ion system built from it, with a companion
repository laying out a complete Rb-87 neutral-atom platform. 147 stars,
last pushed 2026-08-04.

This is domain A2, working, in FreeCAD, with physics behind it. It is the
single strongest piece of evidence that the first draft's scoping was
wrong.

**It also has no LICENSE file.** The GitHub API reports no license for the
repository, and there is none at any conventional path. Under the default
that means **all rights reserved**: it cannot be forked, vendored, adapted
or shipped, regardless of how open the intent obviously is. This is almost
certainly an oversight by a physics group rather than a position -- but
until it is fixed, PyOpticL is **prior art to read and learn from, and
code to keep at arm's length**.

Concrete action, and cheap: ask them to add one. An MIT or BSD-3 grant on
a tool this useful would matter to more than this fork.

What to take from it regardless: **beam-path-driven placement**. The user
does not position a mirror and then ask where the beam goes; the user
declares the beam path and the components land on it. That is the right
interaction model for A2, and it inverts the one every optics addon here
uses.

### The RF and EM niche is already occupied -- do not duplicate C3

- **`king-aj3/EMStudioFree`** (**LGPL-2.1**) -- a FreeCAD workbench for RF
  and EM simulation (antennas, S-parameters, far fields, magnetics, cable
  design) driving openEMS, NEC2, Elmer and Palace.
- **`LubomirJagos/FreeCAD-OpenEMS-Export`** (GPL-3) -- GUI plugin
  exporting a FreeCAD model to openEMS; last pushed 2023, but 109 stars.
- **`snhobbs/FreeCAD-FDTD-Workbench`** -- an HFSS-shaped workflow over
  openEMS: assign materials, ports, lumped parts and excitation to model
  objects.
- **`ediloren/EM-Workbench-for-FreeCAD`** -- FreeCAD as pre-processor to
  FastHenry and FasterCap.

Every one of these is the same pattern: **FreeCAD as the geometry and
property front end, a GPL solver behind a process boundary.** That is
exactly the architecture section 8 arrives at independently, already
working in this program, four times over. It is also why C3 is off the
list -- the niche is filled by people who care about it more.

### Commercial validation of A3

Ansys sells NEST (Nested Elements and Systems Tolerancing) and
optomechanical tolerancing inside Speos, and their own documentation names
the problem in the terms section 1 uses: mechanical pivot points sit away
from the optical axis and are determined by the mount design;
misalignments in one assembly propagate through the next; and doing this
in Zemax historically meant hand-writing complex tolerance operand
structures, which was error-prone and slow. A3 is a job people pay
commercial prices to have done, in a CAD-based environment, and there is
no free answer at all.


## 7. Library survey, licenses verified

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
| **PyOpticL** | **NONE** | Python (FreeCAD) | **A2** | addon | Beam-path-driven layout and baseplates, Gaussian beams. Published, active -- and **legally unusable**, section 6. |
| **freecad-pyoptools** | GPL-3 | Python (FreeCAD) | A | addon | pyOpTools tracing inside FreeCAD. Addon only, never core. |
| **gdsfactory** | **MIT** | Python | **C1 layout** | PyPI | The PIC layout tool. Owns C1; do not compete. Emits GDS/OASIS/STL. |
| **MEOW** | **Apache-2.0** | Python | C1 EME | PyPI | Eigenmode expansion for tapers and mode converters. Leans on the tidy3d FDE solver. |
| **femwell** | **GPL-3** | Python | C1 modes | PyPI | FEM waveguide modes, thermal, electro-optic. Copyleft. |
| **MPB** | **GPL-2.0** | C | C1 bands | conda | MIT Photonic Bands. Copyleft. |
| **tidy3d** | LGPL-2.1 client | Python | C1/D | PyPI | Client is LGPL; **the engine is a paid cloud service**. Not a local solver. |
| **EMStudioFree** | **LGPL-2.1** | Python (FreeCAD) | **C3** | addon | Already serves the RF niche, over openEMS/NEC2/Elmer/Palace. |

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

**In photonics, the layout is permissive and the physics is copyleft.**
gdsfactory is MIT and MEOW is Apache-2.0; femwell is GPL-3, MPB is GPL-2,
Meep is GPL-2-or-later, and tidy3d's local client wraps a paid cloud
engine. So for C1 the licence line falls exactly where the process
boundary would go anyway -- adopt the permissive layout side, keep every
solver behind a boundary. That is not a constraint to work around, it is
the architecture arriving for free.

**And the A2 row is the worst case of all: a working tool that cannot be
used.** PyOpticL is the only entry in this table that does the job, is
active, is published, and cannot legally be touched. Section 6.


## 8. License compatibility with this fork

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
  catalogs cannot; read them from the user's copy (section 5).

**"No license" is worse than GPL, not better.** GPL code can at least be
used across a process boundary and read freely. Code with no licence grant
-- PyOpticL today -- carries the default of all rights reserved: no fork,
no vendoring, no adaptation, no redistribution. When surveying a field
where much of the good work comes from physics groups rather than software
projects, **check for a LICENSE file before reading the code**, because
having read it is itself a complication if the intent is later to write
something similar.

One trap worth naming because it is easy to wave away: **Python is not a
loophole.** A GPL-3 Python library imported by a workbench raises the same
derivative-work question as linking a shared library -- the FSF's position
is that import *is* linking. "It is only a Python addon" does not make
pyoptools or a Goptical binding safe to depend on. If a GPL engine is
wanted, it goes behind a process boundary like CalculiX, deliberately.


## 9. Recommendation

**Yes, a serious optics workbench is possible here, and its subject is the
four CAD-native jobs of section 1: A2 layout, A3 tolerancing, B
illumination, C2 Gaussian beams -- built on the one shared trace core of
section 2.**

The ordering principle is not difficulty and not ambition; it is **how
much of the answer depends on geometry this program already owns**. By
that measure A3 is first in value and B is first in effort, which is why
the staging in section 12 delivers A2 and A3 before B rather than treating
them as warm-ups.

Six parts:

0. **Build the shared trace core**: rays and beams against exact document
   geometry at real placements (section 4). It is the foundation for all
   four CAD-native jobs and the thing no free tool has.

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
   layer of section 4 that no renderer has.
3. **Take physical optics from the permissive Python set** -- POPPY,
   prysm, HCIPy, LightPipes -- rather than writing propagation. And
   consider feeding **Poke** our ray data: it does polarization ray
   tracing and Gaussian beamlets but today speaks only to Zemax and CODE
   V, so an open ray source would be a contribution worth making upstream.
4. **Keep FDTD out.** Meep is GPL and a different discipline. If it ever
   lands it lands as an external process, like CalculiX.
5. **Do not compete in PIC layout or in RF.** `gdsfactory` (MIT) owns C1
   layout and is good; EMStudioFree and the openEMS plugins already serve
   C3. The defensible C1 role here is **packaging and thermo-mechanical**
   -- fibre attach, submount, lid, stress -- which is solid modelling and
   therefore ours. Interop with gdsfactory (it already emits STL) beats
   reimplementation.
6. **Ask the PyOpticL authors to license their work** (section 6). It
   costs one issue, and it is the difference between the best prior art
   for A2 being a reference and being a foundation.

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


## 10. Where the boundary goes: optics is Boundary C too

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


## 11. What it looks like in this fork's idiom

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


## 12. Staging

Ordered so each stage is useful even if the next never happens -- the same
discipline `docs/Simulator.md` section 8 uses. **Reordered from the first
draft**: section 2 found that the exact-surface core is not a prerequisite
to be got through but the shared foundation of four jobs, and that the
cheapest consumer can validate it long before the most expensive one
exists. So it moves early, and A2/A3 land before B.

- **Stage 0 -- the unit and radiometry contract.** Length, wavelength,
  flux, irradiance, the V(lambda) weighting, and how each crosses Boundary
  C. Written down, shared with the simulator, before any code. Costs
  nothing; deferring it is the failure mode section 10 describes.
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
- **Stage 3 -- the exact optical surface: the shared core.** The piece
  nothing else has: analytic intersection for sphere, conic, even-asphere,
  toroid and cylinder, plus Newton refinement onto a general OCCT surface,
  traced against real placements in a real assembly. Benchmark against the
  tessellated answer so section 4's estimate becomes a measured number.
  **This stage decides whether the whole thing is an instrument**, and
  section 2 says it is the foundation of A2, A3, C2 and B alike -- so it
  is the centre of the plan, not a gate before the interesting part.
- **Stage 3b -- A2, optical layout.** Beam-path-driven placement (the
  PyOpticL interaction model, section 6): declare the path, let components
  land on it, re-route when something moves. This is the **cheapest
  consumer of stage 3 and its first validation** -- a beam that lands
  where a protractor says it should is a test anyone can check. Delivers a
  real workflow to a real user community with no new dependency and no
  engine decision.
- **Stage 3c -- A3, optomechanical tolerancing.** Perturb the mechanical
  parameters, re-run the trace, report the beam statistics. Its thermal
  extension is reachable (section 12a) once one generic piece of plumbing
  exists: a bridge from an FEM displacement result to a `Placement`. Monte Carlo
  over document properties, which the expression engine and property
  system already provide as a parameter space. **This is the highest-value
  stage in the document** (section 1): nothing free does it, Ansys charges
  for it, and it needs stage 3's exactness and nothing else. It is also a
  Boundary C job in its own right -- a long run over document state
  emitting a derived dataset -- so it exercises section 10's protocol
  cheaply, before fluids or full illumination do.
- **Stage 3d -- C2, Gaussian beams.** The same trace carrying a
  q-parameter instead of a bare direction: waists, mode matching, fibre
  and cavity coupling efficiency. Small once stage 3 exists, and it is
  what makes stage 3b useful to the lab community rather than merely
  pretty.
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
  and diffraction; consider the Poke contribution from section 9.
- **Stage 9 -- optimisation over the CAD parameter space.** Stage 3c
  computes what a perturbation costs; this searches for the parameters
  that minimise it. Same machinery, inverted, and it is what zaphB's
  workbench pointed at.
- **Stage 10 -- PIC packaging interop (optional, narrow).** Import a
  `gdsfactory` cross-section or STL and do the thermo-mechanical and
  fibre-attach work in the document. Explicitly *not* a layout tool.

A small item that sits outside this order and is worth doing whenever
convenient: **a prescription-to-solid feature** -- a parametric lens
element from radii, thickness, glass, conic and asphere terms. It is
small, obviously useful on its own, bridges domain A output into the
document, and would earn its place even if nothing else on this list
happens.


## 12a. Does this fork have a thermal solver good enough for A3?

Section 15 of the first revision left this open, on the assumption that
coupling thermal analysis into a tolerance study was "a much larger claim
than perturbing placements". **Checked against the tree, that assumption
was too pessimistic. The solver side is done; the gap is a bridge, and it
is small and generic.**

### What is actually wired

**CalculiX** (`femsolver/calculix/`), all verified in this tree:

- `*HEAT TRANSFER`, steady state and transient
  (`write_step_equation.py:73-88`).
- `*COUPLED TEMPERATURE-DISPLACEMENT` and
  `*UNCOUPLED TEMPERATURE-DISPLACEMENT` -- real thermomechanical coupling,
  not a two-step hand-off.
- `*FILM` convection, `*RADIATE` radiation, and
  `*RADIATE, CAVITY=` cavity radiation with open and closed cavities
  (`write_constraint_heatflux.py:50-68`).
- `*DFLUX` body heat source (`write_constraint_bodyheatsource.py`).
- `*GAP CONDUCTANCE` -- thermal contact resistance across an interface,
  from a `ThermalContactConductance` property
  (`write_constraint_contact.py:124`). This one matters for optomechanics:
  a bolted mount's thermal path is dominated by contact conductance.

The constraints are first-class C++ objects -- `FemConstraintTemperature`,
`FemConstraintInitialTemperature`, and `FemConstraintHeatflux` with
`Flux` / `Convection` / `Radiation` types plus `FilmCoef`, `AmbientTemp`,
`Emissivity`, `CavityRadiation` and `ClosedCavity`
(`App/FemConstraintHeatflux.cpp:33-56`).

**Elmer** (`femsolver/elmer/equations/heat*.py`) carries an independent
heat equation: temperature and heat-flux boundaries, convection, radiation
as `Diffuse Gray` with open or closed cavity, and a phase-change model.
Its elasticity writer reads `ThermalExpansionCoefficient` against a
`Reference Temperature` taken from the initial-temperature constraint, so
thermoelastic coupling exists on that path too. **Two independent thermal
solvers that can be cross-checked** is the same cheap-correctness argument
section 9 makes for Optiland versus rayoptics.

Regression and example coverage exists rather than being aspirational:
`thermomech_bimetal` is both an example and a test case
(`femtest/app/test_ccxtools.py:333`), alongside `ccx_cavity_radiation`,
`ccx_radiation_benchmark` (added upstream in 2026) and
`inductive_heating_axisymmetric`.

**A correction worth recording**, because the wrong version is repeated
widely: **CalculiX computes cavity view factors itself.** It triangulates
the interacting surfaces and uses Lambert's analytical reduction of the
four-fold view-factor integral to a two-fold one, with one-point
integration at the base triangle's centroid and an accuracy knob in
`radmatrix`. A search summary told this document that view factors had to
be supplied externally; the CalculiX documentation says otherwise. Cavity
radiation here is a real capability, not a stub.

### The four gaps, in the order they would hurt

1. **There is no FEM-result-to-`Placement` bridge, and this is the one
   that has to be built.** `femresult/resulttools.py:147` calls
   `Mesh.ViewObject.applyDisplacement()`, which scales the *mesh
   visualisation*. Nothing writes a computed displacement back onto a
   document object's `Placement`. So today the fork can compute that a
   mount face moves 12 um and tilts 40 urad, and has no way to hand that
   to an optical trace. That is precisely the A3 coupling, and it is
   **small, generic, and useful beyond optics** -- any deformed-assembly
   workflow wants it.
2. **No temperature-dependent material properties.**
   `write_femelement_material.py:128-135` writes `*CONDUCTIVITY`,
   `*EXPANSION` and `*SPECIFIC HEAT` as a single constant line each.
   CalculiX accepts temperature tables; FreeCAD does not write them. **A
   writer gap, not a solver gap**, and cheap to close. For optomechanics
   near room temperature it barely matters -- CTE moves a few percent over
   plus or minus 20 K -- but it is disqualifying for cryogenic or furnace
   work.
3. **Convection coefficients are the user's problem.** There is no
   conjugate heat transfer in core: you supply a film coefficient and an
   ambient temperature. CfdOF (OpenFOAM, GPL, external addon) is the CFD
   path and is not in this tree. For a bench in still air a handbook
   coefficient is fine; for a forced-air enclosure it is a guess with the
   answer's accuracy riding on it.
4. **Optical thermal data is absent on both sides.** The material library
   has three glass cards and all three are *structural* fibreglass
   (`Material/Resources/Materials/Standard/Glass/`). More importantly
   **nothing anywhere carries dn/dT**, the thermo-optic coefficient -- and
   in a transmitting element the index change with temperature is often
   comparable to the mechanical motion, so a thermal drift study that
   models only expansion is answering half the question. This is an
   optics-side data job, not a FEM defect, and section 5's CC0 catalog
   plus vendor CTE and dn/dT tables is where it comes from.

### Verdict

**Good enough, and better than the open question assumed.** For the A3 job
-- a slow thermal excursion over a bench or an instrument, producing
placement perturbations of optical components -- CalculiX's coupled
temperature-displacement analysis with contact conductance and cavity
radiation is the right tool, it is already wired, and it is already
tested. Constant material properties are adequate at the temperatures
optomechanics cares about.

What is missing is not physics. It is (a) the result-to-placement bridge,
(b) dn/dT on optical materials, and (c) temperature tables in the material
writer, which is a small fix. None of the three is a research problem.

So thermal A3 moves from "a much larger claim, do not assume into scope"
to **a reachable stage 3c extension**, gated on one generic piece of
plumbing that the fork would benefit from having anyway.


## 13. What "physically accurate" has to survive

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


## 14. The honest case against

Stated plainly, because the case for is easy to over-sell.

- **The validation burden is heavier than any other workstream in this
  fork.** The FEM port had CalculiX to lean on; here we would *be* the
  reference implementation for domain B. In this field being confidently
  wrong is worse than being absent, and the users can tell.
- **A1 is already free and already good.** A FreeCAD wrapper around
  Optiland is a nicety and wins nobody. This much of the first draft's
  scepticism survives -- it was only ever wrong about A2 and A3.
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
- **The A2/A3 audience is small and specialised.** Optics labs and
  optomechanical engineers are not the median FreeCAD user, and building
  for them is a bet on depth rather than reach. The counterweight is that
  it is a bet on people who currently have no option at all, and who
  publish -- PyOpticL's paper is what put A2 on this document's map.
- **Scope now spans four jobs rather than one**, which is honest but is
  also more surface to get wrong. The mitigation is section 2: they share
  a core, so the risk concentrates in stage 3 rather than spreading. If
  stage 3 fails, all four fail together and early, which is the good kind
  of coupling.

And the case for, in one sentence: **there is no free tool for optical
layout, optomechanical tolerancing, non-sequential illumination or
mode-matching on a real bench, all four need the same thing, and this fork
is closer to having that thing than any other project is** -- because it already owns a
geometry kernel, a Monte Carlo transport engine with Embree under it, a
material and per-face system, a shader graph, and a streaming display
tier. That is not a small distance already covered.


## 15. Open questions

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
- **Who is the first user?** The revised staging answers "the optics-lab
  builder" (A2/A3) rather than the first draft's "stray-light analyst",
  because that user is reachable two stages earlier and needs no engine
  decision. A lighting designer, a lens designer and a photonics
  researcher still want three further programs. That should stay a
  decision, not a drift.
- **Which "waveguide" is in scope?** Section 1 splits it three ways -- PIC
  (C1, packaging only), Gaussian beams on a bench (C2, in scope and
  cheap), RF and mm-wave (C3, already served). The staging assumes C2 and
  a narrow C1 packaging role. If integrated photonics is actually wanted
  as a first-class target, that is a different document with gdsfactory at
  its centre.
- **Does A3 need a mechanics solver?** Partly answered in section 12a: the
  thermal solver is there and is good, and the missing piece is a small
  generic bridge rather than a physics gap. What stays open is whether
  stress birefringence (a tensor field on a transmitting element, not a
  placement perturbation) is in scope, which is a much larger claim than
  thermal drift and should not be assumed in.
- **Should PyOpticL be approached as collaborators rather than prior art?**
  They have the A2 interaction model, the published validation and the
  user community; this fork has the kernel, the exact surfaces and the
  renderer. The licence issue has to be settled either way.
