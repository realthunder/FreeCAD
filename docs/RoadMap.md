# FreeCAD (realthunder fork) — Roadmap

Status: living document. Captures the direction and phased plan for this fork.
Last substantive update: 2026-07. Annotated 2026-07-29 against what has actually shipped.

**Reading convention.** Nothing is deleted here — the original plan is part of the
record. Instead:

- ~~Struck text~~ is superseded: it describes work that has since been done, or a
  choice that has since been made differently.
- <span style="color:#1a7f37">**Done (date).** green text</span> records work that has
  **landed in the tree**, with the mechanism named.
- <span style="color:#9a6a12">**Decided (date).** amber text</span> records a question
  that has been **settled but not (or not yet fully) built**.

Unstruck, unannotated text is still open plan. The per-subsystem records these
annotations draw on: [RendererPlan.md](./RendererPlan.md) (renderer phase log),
[RenderEngine.md](./RenderEngine.md) (renderer + user-shader architecture),
[TShapeRenderCache.md](./TShapeRenderCache.md) (instancing),
[SceneStreaming.md](./SceneStreaming.md) (streamed browser scenes),
[RenderDebug.md](./RenderDebug.md) (verification harness),
[FileBlobsManager.md](./FileBlobsManager.md) (content-addressed document files),
[MultiDocServe.md](./MultiDocServe.md) (multi-document serving, the session/user model and
how process-per-session scales),
[ThinClient.md](./ThinClient.md) + [ViewerUIResearch.md](./ViewerUIResearch.md)
(client UI — design/research only).

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
battle-tested **first and cheaply** — ~~a v0 MCP surface over the existing in-process Python
API~~ — before any expensive machinery is committed underneath it.
<span style="color:#1a7f37">**Done (2026-07).** The v0 MCP surface exists and is in daily
use: `freecad.mcp_console` (`src/Ext/freecad/mcp_console/`) serves `run_python` and
`search_api` over FastMCP Streamable-HTTP at `http://127.0.0.1:8765/mcp`, marshalling each
call onto FreeCAD's Qt main thread (the `Web::AppServer` pattern) so document/OCCT/Coin work
is safe. The session is a persistent REPL that captures stdout/stderr and returns exceptions
as text. See [DevEnvironment.md](./DevEnvironment.md) §"MCP debug console". What it is *not*
yet: schema'd semantic operations, transactions or structured errors — it is deliberately a
Python passthrough, so the protocol design questions below are all still open.</span>

## Workstreams

### 1. Renderer
- Current: `src/Gui/Renderer/` has a backend-agnostic `Renderer`/`RendererFactory`
  abstraction with **bgfx** and **Diligent** backends, ~~both proof-of-concept (demo cube),~~
  composited on top of Coin's GL output.
  <span style="color:#1a7f37">**Done (2026-07).** The bgfx backend renders **real model
  geometry** and is the working renderer of the fork; the demo cube is long gone.
  `SoFCRendererBridge` translates `SoFCRenderer`'s draw lists into `Render::DrawCall` /
  `Render::Material` + per-frame configs (see [RenderEngine.md](./RenderEngine.md) §2).
  RendererPlan phases 0, 1, 2 and 2b are largely done: CAD parity (line widths/patterns,
  point sprites, hidden-line and outline draw styles, clip planes, textures, section caps
  with hatch), WBOIT transparency, SSAO, PBR + IBL, bump/normal/parallax mapping, shadows
  (cached VSM → EVSM, directional + spot, ground plane with texture/bump/transparency),
  volumetric light shafts, water as a bounded medium plus water surface / glass / cloud /
  fire media and caustics, ground reflection, per-object material properties with a task
  panel and prefs page, and a glTF metallic-roughness material round trip through the
  Import module. On top of that sits a **user-programmable shader framework** (stages
  material / water / volume / particle / post, `Appearance` link-group binding with
  Object / Instance / Element scope, property-bound uniforms, a bundled effect library,
  browser-tier compile) — [RenderEngine.md](./RenderEngine.md) — and a render
  **verification harness** (golden images, frame dumps, `saveRenderDump`/`getRenderStats`,
  desktop + headless-Chromium legs), [RenderDebug.md](./RenderDebug.md) phases 1-6. The
  Diligent backend was *not* developed further and remains a compiling POC hedge.</span>
- Direction:
  - Make the chosen backend the **sole renderer**; demote Coin3D from *renderer* to
    *scene-data structure*. ~~Bridge `src/Gui/Inventor/SoFCRenderCache*` geometry into the
    backend.~~ The "composite on top of Coin GL" model is scaffolding, not the end state
    (it can't reach the browser — Coin is desktop GL).
    <span style="color:#1a7f37">**Done (2026-07), the bridge.** Geometry is fed behind
    `SoFCRenderer` (its GL emission is skipped per frame via `canSkipInternal()`), which is
    the single choke point for all five feeds. Coin is already demoted for the model itself,
    and the raw-GL overlays that used to force a Coin pass — axis cross, rubber band /
    polyline, fps text, NaviCube, datum labels, and `SoText2` generally via the reusable
    `Gui::SoTextImage` — have been ported to the backend feed (RendererPlan overlay
    Coin-ification A/B/C + phase D). **Still true:** Coin remains the scene-data structure
    and still composites on desktop; "sole renderer" is not reached.</span>
  - ~~**bgfx** is the pragmatic near-term choice (mature WebGL2/GLES/Metal/Vulkan, one
    shader toolchain, tiny, BSD). **Diligent** is the WebGPU-forward hedge (ships WebGPU +
    compute-in-browser + PBR/post-fx as of v2.5.6). Keep both behind `RendererFactory` and
    let the WebGPU maturity curve break the tie.~~
    <span style="color:#9a6a12">**Decided (2026-07).** bgfx is *the* primary backend, not
    merely the near-term one — see [RendererPlan.md](./RendererPlan.md) §2 for the recorded
    reasoning (Diligent's Metal backend is closed-source/commercial, bus factor 1, no tagged
    release since 2024-09; bgfx's WebGL path is the browser story we need first). The
    `Render::Renderer` abstraction stays backend-neutral and the Diligent POC keeps
    compiling as a hedge; re-evaluate wgpu-native/Dawn in ~12 months. The same section also
    records **Qt3D / Qt Quick 3D + Qt-for-WebAssembly as rejected** (2026-07), on four
    grounds.</span>
  - Large-model rendering (culling / LOD / instancing / batching) is a **scene-management**
    effort *above* the backend — the real perf lever, orthogonal to backend choice.
    <span style="color:#1a7f37">**Done in part (2026-07).** Instancing is complete down to
    the TShape level — one `TopoDS_TShape` tessellated once, drawn as GPU instances, with
    colour-variant branching, shared geometry buffers, line/point variants, per-instance
    sub-element highlight and instanced depth/shadow passes
    ([TShapeRenderCache.md](./TShapeRenderCache.md)). CPU frustum culling runs per frame in
    the backend. **Not done:** LOD, occlusion culling, and import-time shape dedup
    (instancing recovery) are still planned.</span>
  - ~~**Particle system** (low priority): GPU-simulated ballistic particles (billboards,
    sim pass, depth-sorted blending, emitter state streamed to the WASM viewer) as the
    close-range upgrade for the procedural volumetric effects (fire, fountain spray,
    splashes). The volumetric plumes stay the mid/far representation; particles layer on
    top without replacing them.~~
    <span style="color:#1a7f37">**Done (2026-07), first cut, and earlier than "low
    priority" implied.** Particles landed as a *stateless* GPU system inside the user-shader
    framework rather than as a simulation pass: a `particle` shader stage over generated
    seed quads (per-particle index + random seed attributes), target-fit emitters driven by
    `Emitter*` program properties, at both Object and Instance scope. Bundled effects ship a
    particle companion program disabled by default (Embers, WaterSpray, Droplets), and Rain
    is a particle-only package. The volumetric plumes did stay the mid/far
    representation.</span>

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
- Full design: [ComputeBoundaries.md](./ComputeBoundaries.md). The serving side's
  session/user model and the cloud-scaling analysis of process-per-session live in
  [MultiDocServe.md](./MultiDocServe.md) §7.
- <span style="color:#9a6a12">**Still entirely unbuilt (as of 2026-07).**</span> None of
  this workstream has landed: no headless document server, no process-per-document split, no
  `execute()` purity contract or audit, no parallel recompute scheduler, no cross-process
  BRep transfer. The one adjacent thing that *did* land is **content addressing in the
  document tier** — `App::PropertyFileIncluded` files are now stored and reference-counted by
  content hash and written as `blobs/` archive entries -- named after the referring
  property, with a `blobs/Content.xml` index binding name to content, so an unpacked
  project is diffable -- with save options and a
  restore handover protocol ([FileBlobsManager.md](./FileBlobsManager.md)). That is
  deduplicated file storage, *not* recompute memoization, and it does not advance the
  process split; it does establish the content-addressing habit both tiers want.
  <span style="color:#1a7f37">**Extended through 2026-08 to the shapes
  themselves** ([SharedShapeStorage.md](./SharedShapeStorage.md)): a stored
  `TopoShape` is a content-addressed file like any other, one file per distinct
  shape, and four preferences cut what those files hold -- pcurve dedup with the
  planar drop, congruent-instance dedup (one file plus a rigid motion for the
  same part in twenty places), cross-file geometry tables, and borrowing a
  sub-shape from another file. On a 17800-object assembly the shape bytes fall
  from 113.9MB to 71.3MB. The arc closed on a measurement, sec 12.16: the one
  piece left was worth nothing at assembly scale. Still not recompute
  memoization, which remains the near-term compute item below.</span>

### 3. WebAssembly tier (browser / mobile / sandbox)
- OCCT + Python already run in the browser via WASM (opencascade.js / occt-wasm; OCP.wasm
  runs build123d under Pyodide) — proof the stack is viable.
- Role of WASM here is the **local preview / offline / sandbox tier**, *not* the
  large-model path (WASM32 has a 4 GB linear-memory ceiling; memory64 is immature).
  <span style="color:#1a7f37">**Built, but as a different thing (2026-07).** The WASM tier
  that exists is a **standalone streamed viewer**, not an in-browser kernel: the same
  `BGFXRenderer.cpp` compiles under Emscripten (`FC_RENDERER_STANDALONE`,
  `Gui/Renderer/wasm/main.cpp`) into a few-MB bundle with no Qt, no Python and no OCCT in
  the browser, fed a `SceneDump` snapshot over WebSocket by `SceneStreamServer`. It ships
  with mouse **and** touch interaction, picking, a mobile shell and hi-DPI handling, the
  ported overlays (axis cross, NaviCube incl. click-to-orient, rubber band, text), its own
  essl shader pack plus server-compiled user-shader binaries, MSAA and AO. It is verified on
  desktop browsers, on a phone, and headlessly under Chromium. This vindicates the *thin
  streamed client* reading of the tier (workstream 4) rather than the "OCCT+Python in the
  browser" reading.</span>
- Run **untrusted embedded document Python** (macros, expressions, scripted objects)
  through a **Pyodide/WASM sandbox** — fixes a real security hole (opening a malicious
  `.FCStd` can currently execute arbitrary code) and is browser-friendly.
  <span style="color:#9a6a12">**Not started (2026-07).** The security hole is still
  open.</span>
- The same WASM modules can run out-of-process on desktop via a native runtime
  (Wasmtime/Wasmer/WAMR + WASI), unifying the sandbox and browser stories.
  <span style="color:#9a6a12">**Not started (2026-07).**</span>

### 4. Large models — how commercial web CAD does it
- Onshape et al. do **not** run the kernel in the browser. Geometry servers hold the
  precise B-rep on 64-bit cloud machines; the client receives only **streamed tessellation**
  (LOD, view-prioritized) rendered with WebGL. The 4 GB limit never applies because the
  browser never holds B-rep.
- Adopt the same mental model: **client = thin renderer of streamed tessellation; B-rep +
  kernel live in the geometry service, referenced by handle.** This is the same headless
  server from workstream 2, viewed from the client side.
  <span style="color:#1a7f37">**Done in large part (2026-07), the client half.** Tessellation
  streaming is built and real ([SceneStreaming.md](./SceneStreaming.md) phases 1, 2, 3 and
  4a). The scene is **content-addressed**: textures, the section hatch, mesh chunks and a
  deduplicated material table leave the stream as SHA-1-keyed blobs fetched out of band
  (`GET /blob?key=`, meshes pulled in byte-budgeted batches), cached in the viewer's
  IndexedDB and verified on read-back. Above them sits a **manifest tree** keyed by
  `objectKey` and a **delta root** — the server keeps a bounded publish history and narrows
  each root to what that viewer is missing. Measured on the reference scene: **425 KB → 1.4
  KB** per publish; on a 200-object benchmark **17,146 B → 1,027 B**. Application is
  progressive and organised as a **fidelity ladder** — a draw names the best rung it holds,
  from a synthesised bounding box up to the full mesh, and climbs it through the ordinary
  delta path — so a publish is drawn while it is still arriving and a missing mesh is a box
  on its bounds, not a hole; picking follows the rung. Chunks record which objects want them
  and the viewer fetches in **camera order**, so the visible part of a model loads first.
  **Not done:** ladder-descending eviction (4b) and per-mesh LOD rungs (5) are still
  specification, and per-arrival assembly is not yet incremental — the benchmark says that
  is now the dominant cost of a streamed load.</span>
  <span style="color:#9a6a12">**Unchanged:** the publisher is still a full desktop FreeCAD
  process, not a geometry service — the client half of this model exists, the server half
  (workstream 2) does not.</span>

### 5. AI-native interface
- Do **not** build a separate AI API — make the headless operation protocol the primary,
  versioned, schema'd, semantic interface that GUI, scripts, remote clients, and AI agents
  all consume.
- Expose it over **MCP** (operations = tools, document/DAG/objects = resources).
  <span style="color:#9a6a12">**Partly answered by v0 (2026-07).** The transport is MCP and
  works, but the surface is deliberately *not* operations-as-tools: two tools only
  (`run_python`, `search_api`), on the reasoning that the whole FreeCAD API is already
  Python-reachable, so the tool description teaches entry points instead of wrapping
  operations. Whether the eventual protocol keeps that shape is still open.</span>
- ~~**Start now, cheaply**: a v0 MCP server over the *existing* in-process Python API — no
  headless split required. Real agent usage is the fastest way to learn what the protocol
  needs before the big refactor commits to it.~~
  <span style="color:#1a7f37">**Done (2026-07).** `freecad.mcp_console` — see "The
  through-line" above. It is in daily use driving the live GUI, and the learning it was
  meant to produce is arriving: `search_api` exists because live API discovery turned out to
  be the agent's real bottleneck.</span>
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
- ~~Renderer: pick the migration path off Coin-GL; prototype the `SoFCRenderCache` → backend
  geometry bridge (replace the demo cube with real model geometry). *(visible payoff)*~~
  <span style="color:#1a7f37">**Done (2026-07) and far overshot.** The bridge is not a
  prototype: it carries the whole shaded model, CAD parity draw styles, the visual-feature
  set, instancing, the WASM/streaming tier and the user-shader framework (workstream 1).
  The migration path chosen was "feed behind `SoFCRenderer`, skip its GL emission per
  frame".</span>
- ~~v0 semantic protocol: MCP server over the existing in-process Python API — schema'd
  operations, semantic queries, structured errors. *(cheapest, highest-information item)*~~
  <span style="color:#1a7f37">**Done (2026-07), in its cheap form.** `freecad.mcp_console`
  ships and is used daily. <span style="color:#9a6a12">The *schema'd operations,
  transactions and structured errors* part was not attempted and remains near-term
  work.</span></span>
- Compute: content-hash memoization of recompute results (standalone win + cache substrate).
  <span style="color:#9a6a12">**Not started (2026-07).** Content hashing exists for included
  document files and for the scene stream, but not for recompute results.</span>
- <span style="color:#1a7f37">**Added and done (2026-07), not on the original list:** the
  render verification harness ([RenderDebug.md](./RenderDebug.md) phases 1-6) — golden-image
  comparison across desktop-GPU and browser legs, frame dumps and stats through a Python
  API, live debug properties, and dynamic named-uniform binding. It was written because the
  renderer had outgrown eyeball verification, and it is what the user-shader framework grew
  out of.</span>

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
  <span style="color:#1a7f37">**Tessellation streaming: done early (2026-07)** — it turned
  out to be a *near*-term item, buildable against the existing in-process document (see
  workstream 4).</span> <span style="color:#9a6a12">Distributed/cloud workers: not
  started.</span>
- WebGPU renderer backend as it matures.
  <span style="color:#9a6a12">**Still long term (2026-07).** bgfx's WebGPU backend is
  native-Dawn-only; browser = WebGL2 for the foreseeable future. Re-evaluate ~2027.</span>

## Open questions
- ~~Which backend wins long-term (bgfx vs Diligent/WebGPU) — decide as WebGPU stabilizes.~~
  <span style="color:#9a6a12">**Decided (2026-07): bgfx**, and all subsequent work is built
  on it — [RendererPlan.md](./RendererPlan.md) §2 (Diligent kept as a compiling POC; Qt3D /
  Qt Quick 3D rejected; WebGPU re-evaluated in ~12 months). What is *not* decided is the
  eventual WebGPU migration, which now reads as a bgfx-backend question rather than a
  choice between engines.</span>
- <span style="color:#9a6a12">**Resolved renderer-local questions (2026-07)**, recorded in
  [RendererPlan.md](./RendererPlan.md) §6: (a) the backend is fed **behind `SoFCRenderer`**,
  not as a parallel sink off the cache manager — it is the single choke point for all five
  feeds, at the cost of one frame of latency that `needsRedraw()` converges away; (b) **PBR
  material parameters live on the ViewProvider** as document-saved,
  glTF-metallic-roughness-shaped properties, with an App-side material model free to map
  onto them later, and interchange going through the Import module's RWGltf/XCAF path. Still
  open there: shared-context vs blit, and per-view renderer instances vs a shared engine
  (the bgfx view-id allocator itself is built — RenderEngine.md §3.1).</span>
- <span style="color:#9a6a12">**Settled by research, not yet built (2026-07-20):** the
  client UI framework question. [ViewerUIResearch.md](./ViewerUIResearch.md) concluded on
  the industry pattern — **WASM/WebGL canvas + browser DOM for all UI chrome** (Figma,
  AutoCAD Web, Onshape), because soft keyboard, IME, accessibility, scrolling, clipboard and
  file dialogs are free and correct in DOM and chronic reimplementation projects in-canvas.
  [ThinClient.md](./ThinClient.md) builds on that decision and is **design only, not
  implemented**; its first deliverable is a selection-driven property inspector, which needs
  an identity table in `SceneDump` and a JSON control channel.</span>
- Where Python runs in the distributed model: co-resident in the geometry worker (favored)
  vs. client-side issuing RPCs (too chatty).
- How to serialize topological-naming state (`StringHasher` is document-global) across a
  process boundary — content-addressing vs. shipping relevant entries.
- Undo/transactions across resident workers: command replay (favors a command-log protocol,
  also suits collaboration) vs worker-state snapshotting.
- Exact shape of the GUI's low-latency local tier: which pieces run client-side (sketch
  constraint solver is the clear first candidate) and how commits reconcile with the server.
- Collaboration/multi-user consistency model (server-authoritative + OT/CRDT) — deferred,
  but don't design it out. The first half-step is designed: a serving backend is one
  collaborative *room* (shared selection, token at the door, identity labels), with
  isolation pushed to a process-per-session gateway — [MultiDocServe.md](./MultiDocServe.md)
  §7.
