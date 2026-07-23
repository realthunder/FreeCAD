# FreeCAD (realthunder fork) — Roadmap

Status: living document. Captures the direction and phased plan for this fork.
Last substantive update: 2026-07.

## Vision

Evolve FreeCAD into a CAD program that runs across **browser, mobile, and traditional
desktop** from one codebase, with:

- better **user interaction**,
- higher **performance on large models**,
- better **rendering**, and
- a **first-class, AI-native automation interface**.

This fork is a **frontier for feature exploration**; strict conformance to upstream
FreeCAD conventions is a non-goal. The Topological Naming Problem is considered
essentially solved and is no longer a focus.

Companion forks are developed alongside this repo (see `CLAUDE.md`):
- `~/works/sw/coin` — forked Coin3D
- `~/works/sw/occt` — forked OCCT (kernel improvement is an explicit focus)

## The through-line

Nearly every goal below collapses onto **one deliverable**: an excellent
**semantic, schema'd, transactional, deterministic document-operation protocol with
resident server-side state.** That single protocol is simultaneously:

- the **GUI ↔ engine boundary** (thin client, browser/mobile),
- the **distribution boundary** (parallel workers, cloud scale), and
- the **AI-agent interface** (automation).

The parallel-recompute machinery lives *underneath* it and stays invisible to all
consumers. One qualifier: the GUI additionally needs a **low-latency local interaction
tier** (client-side sketch solving, local previews) — the semantic protocol carries
operations and commits, not 60 Hz feedback. Design details are in
[ComputeBoundaries.md](./ComputeBoundaries.md).

Because the protocol is the most important interface in the design, it gets built and
battle-tested **first and cheaply** — a v0 MCP surface over the existing in-process Python
API — before any expensive machinery is committed underneath it.

## Workstreams

### 1. Renderer
- Current: `src/Gui/Renderer/` has a backend-agnostic `Renderer`/`RendererFactory`
  abstraction with **bgfx** and **Diligent** backends, both proof-of-concept (demo cube),
  composited on top of Coin's GL output.
- Direction:
  - Make the chosen backend the **sole renderer**; demote Coin3D from *renderer* to
    *scene-data structure*. Bridge `src/Gui/Inventor/SoFCRenderCache*` geometry into the
    backend. The "composite on top of Coin GL" model is scaffolding, not the end state
    (it can't reach the browser — Coin is desktop GL).
  - **bgfx** is the pragmatic near-term choice (mature WebGL2/GLES/Metal/Vulkan, one
    shader toolchain, tiny, BSD). **Diligent** is the WebGPU-forward hedge (ships WebGPU +
    compute-in-browser + PBR/post-fx as of v2.5.6). Keep both behind `RendererFactory` and
    let the WebGPU maturity curve break the tie.
  - Large-model rendering (culling / LOD / instancing / batching) is a **scene-management**
    effort *above* the backend — the real perf lever, orthogonal to backend choice.
  - **Particle system** (low priority): GPU-simulated ballistic particles (billboards,
    sim pass, depth-sorted blending, emitter state streamed to the WASM viewer) as the
    close-range upgrade for the procedural volumetric effects (fire, fountain spray,
    splashes). The volumetric plumes stay the mid/far representation; particles layer on
    top without replacing them.

### 2. Headless engine + parallel/distributed compute
- Turn the **entire non-GUI side into a headless document server**; the GUI becomes a
  thin client of the operation protocol. Staged: first route GUI *mutations* through the
  protocol over an in-process transport (the `src/Gui` → App pointer coupling makes the
  full split the most invasive change in the plan — pay it incrementally).
- **First process boundary: process-per-document.** Whole-document workers need no
  `execute()` purity contract, and with `App::Link`-based assemblies each linked
  part-document recomputing in its own process is already real parallelism + crash
  isolation. Intra-document DAG partitioning is the later refinement.
- Then refine `App::Document` recompute to **partition the dependency DAG into independent
  sub-graphs** — first in-process threads (C++ features), then separate worker processes,
  then across machines (cloud scale). Gated on the purity audit, which is realistically a
  years-long effort.
- Sync results across process boundaries **without chattiness** via DAG-edge-granular
  bulk transfer, shared-memory binary BRep, and content-hash memoization.
- Full design: [ComputeBoundaries.md](./ComputeBoundaries.md).

### 3. WebAssembly tier (browser / mobile / sandbox)
- OCCT + Python already run in the browser via WASM (opencascade.js / occt-wasm; OCP.wasm
  runs build123d under Pyodide) — proof the stack is viable.
- Role of WASM here is the **local preview / offline / sandbox tier**, *not* the
  large-model path (WASM32 has a 4 GB linear-memory ceiling; memory64 is immature).
- Run **untrusted embedded document Python** (macros, expressions, scripted objects)
  through a **Pyodide/WASM sandbox** — fixes a real security hole (opening a malicious
  `.FCStd` can currently execute arbitrary code) and is browser-friendly.
- The same WASM modules can run out-of-process on desktop via a native runtime
  (Wasmtime/Wasmer/WAMR + WASI), unifying the sandbox and browser stories.

### 4. Large models — how commercial web CAD does it
- Onshape et al. do **not** run the kernel in the browser. Geometry servers hold the
  precise B-rep on 64-bit cloud machines; the client receives only **streamed tessellation**
  (LOD, view-prioritized) rendered with WebGL. The 4 GB limit never applies because the
  browser never holds B-rep.
- Adopt the same mental model: **client = thin renderer of streamed tessellation; B-rep +
  kernel live in the geometry service, referenced by handle.** This is the same headless
  server from workstream 2, viewed from the client side.

### 5. AI-native interface
- Do **not** build a separate AI API — make the headless operation protocol the primary,
  versioned, schema'd, semantic interface that GUI, scripts, remote clients, and AI agents
  all consume.
- Expose it over **MCP** (operations = tools, document/DAG/objects = resources).
- **Start now, cheaply**: a v0 MCP server over the *existing* in-process Python API — no
  headless split required. Real agent usage is the fastest way to learn what the protocol
  needs before the big refactor commits to it.
- Requirements: structured errors (not crashes/tracebacks), transactions (do/undo/rollback),
  deterministic replayable results, cheap **semantic** state queries (DAG, properties,
  measurements, bbox — not pixels), a **sandboxed codegen surface** (the Pyodide sandbox)
  for "agent writes a script, runs it, gets structured results", and on-demand headless
  render for visual feedback.
- Crash isolation and the `execute()` purity/determinism contract (workstream 2) are
  *themselves* AI-friendliness features.

## Phasing (indicative, not dated)

Scope discipline: each phase must deliver **standalone user-visible or learning value**, and
later phases proceed on *evidence* from earlier ones, not momentum. This roadmap is several
team-years of work; the near-term list is deliberately small.

**Near term**
- Renderer: pick the migration path off Coin-GL; prototype the `SoFCRenderCache` → backend
  geometry bridge (replace the demo cube with real model geometry). *(visible payoff)*
- v0 semantic protocol: MCP server over the existing in-process Python API — schema'd
  operations, semantic queries, structured errors. *(cheapest, highest-information item)*
- Compute: content-hash memoization of recompute results (standalone win + cache substrate).

**Mid term**
- **Process-per-document** workers (no purity contract needed): crash isolation, GIL escape,
  assembly-level parallelism via linked documents; builds the serialization plumbing.
- Route GUI mutations through the protocol (in-process transport) — begin the client split.
- `execute()` purity contract + `relocatable` audit (start Part/PartDesign; long-running).
- In-process parallel recompute scheduler (C++ features, threads) — prove parallel == serial.
- WASM sandbox for untrusted document Python.
- Decide the undo model for resident workers (command replay vs state snapshot) — constrains
  the protocol; do not defer past protocol v1.

**Long term**
- Intra-document out-of-process recompute (shared-memory binary BRep, resident sticky
  workers) — gated on the purity audit.
- Element-map/StringHasher serialization across process boundary (content-addressed hasher).
- Headless server completion (GUI as true thin client + local interaction tier).
- Distributed/cloud workers (network transport); tessellation streaming to thin clients.
- WebGPU renderer backend as it matures.

## Open questions
- Which backend wins long-term (bgfx vs Diligent/WebGPU) — decide as WebGPU stabilizes.
- Where Python runs in the distributed model: co-resident in the geometry worker (favored)
  vs. client-side issuing RPCs (too chatty).
- How to serialize topological-naming state (`StringHasher` is document-global) across a
  process boundary — content-addressing vs. shipping relevant entries.
- Undo/transactions across resident workers: command replay (favors a command-log protocol,
  also suits collaboration) vs worker-state snapshotting.
- Exact shape of the GUI's low-latency local tier: which pieces run client-side (sketch
  constraint solver is the clear first candidate) and how commits reconcile with the server.
- Collaboration/multi-user consistency model (server-authoritative + OT/CRDT) — deferred,
  but don't design it out.
