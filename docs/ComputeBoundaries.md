# Compute Boundaries — Headless Engine, Parallel Recompute, and the Unified Protocol

Status: design notes / direction. Not yet implemented.
Companion to [RoadMap.md](./RoadMap.md).

This document details the architecture for turning the non-GUI side of FreeCAD into a
headless engine, parallelizing `App::Document` recompute across processes/machines, and
exposing a single semantic protocol that serves the GUI, remote clients, and AI agents.

## 1. Two boundaries, kept separate

There are two distinct process boundaries. Do not conflate them.

- **Boundary A — GUI client ↔ headless document server.**
  One server per document/session. The whole non-GUI side (App layer, all module
  `App/` code, OCCT, Python) lives in the server. The GUI is a thin client that sends
  operations and renders streamed tessellation. Gives: browser/mobile thin client,
  GUI-process crash isolation, and the AI interface (§6). Why this per-session process
  model scales as a cloud service — idle ≈ 0 CPU, no rendering tier, RAM-bound density,
  hibernate-to-`.FCStd` — is analyzed in [MultiDocServe.md](./MultiDocServe.md) §7.1.

  *Staging realism:* `src/Gui` today assumes direct pointers into App objects everywhere —
  ViewProviders hold `DocumentObject*`, commands poke properties, selection walks the object
  graph. Fully splitting this is the most invasive change in the whole plan. Stage it: keep
  GUI+App in-process but force all **mutations** through the protocol layer over an
  in-process transport first. That validates the protocol and installs the discipline
  without paying the full proxy-object rewrite up front.

  *Interaction realism:* the semantic protocol serves **operations** (create feature, set
  property, commit), not 60 Hz interaction. Sketch dragging, snapping, selection highlight,
  and rubber-banding need sub-millisecond feedback that no request/response protocol
  provides. Plan a **two-tier client**: a low-latency local tier (the sketcher constraint
  solver is small and self-contained — run it client-side, validate on commit; local
  tessellation manipulation for previews) on top of the semantic operation tier. "GUI is
  just another client" is true for operations only.

- **Boundary B — intra-server parallel workers.**
  *Inside* the server, independent sub-graphs of the recompute DAG are dispatched to
  worker threads/processes. Gives: intra-document parallelism and geometry-op crash
  isolation. Usually same-machine ⇒ can use **shared memory** and never pays network cost.

Layering consequence: the GUI/AI client only ever sees Boundary A's coarse protocol. All
worker-sync complexity is internal to the server (Boundary B) and invisible outside it.

## 2. Why a coarse boundary avoids chattiness

Chattiness comes from **query granularity** — making `TopoShape` itself an RPC object means
every `face(i)`/`edge(j)` access is a round-trip. That is the trap; do **not** put the
boundary there.

The recompute DAG already defines a coarse data-flow: each `DocumentObject::execute()`
consumes its input links' result shapes and produces its own result shape. So the only data
that must cross a process boundary is a **finished result shape on a DAG edge that crosses
the partition**, transferred **once per recompute per crossing edge**. That is bulk, not
chatty — a handful of blob transfers per cycle, not thousands of accessor calls.

The remaining engineering is therefore only: (a) *which* edges cross (partitioning), and
(b) *how* to move a shape across cheaply (transport).

## 3. Parallel sub-DAG recompute + result sync

Framed as **graph partitioning to minimize edge cut**:

1. **Partition the DAG** into balanced sub-graphs that minimize cross-partition edges
   (min-cut / METIS-style, weighted by estimated compute cost per node). FreeCAD DAGs
   cluster conveniently: a Part body is a chain; independent parts/bodies are separate
   clusters; the assembly join is the (few but heavy) cut edges. Natural partition:
   *independent parts → separate workers; assembly-level links = the cut set.*

2. **Only cut-edge shapes serialize.** Interior edges stay in-process at native speed.
   Coarse partitions ⇒ few cuts ⇒ little transfer.

3. **Cheap transport for cut-edge shapes:**
   - **Binary BRep** (`BinTools`), never ASCII BRep.
   - **Shared-memory arena / memfd** for same-machine workers — serialize once, pass a
     handle; near-zero-copy handoff. This is the big local win.
   - Network + binary BRep only when workers spill to other machines.

4. **Content-hash memoization.** Key each object's result by
   `hash(own parameters + input result hashes)`. If nothing upstream changed, reuse the
   cached (already-serialized) shape — no recompute *and* no transfer. This subsumes the
   current "touched" flag with a content-addressed cache, and is what makes steady-state
   editing cheap. It also enables distributed cache reuse and reproducibility.

   *Design constraints (do not skip):*
   - **Float parameters**: hash the exact bit pattern; any epsilon/rounding policy belongs
     at edit time, never at hash time (epsilon-hashing breaks transitivity).
   - **Kernel version in the key**: an OCCT upgrade can silently change results for the
     same inputs — include the OCCT (and relevant module) version in the hash key.
   - **No hidden inputs**: `execute()` implementations that consult global tolerance/mode
     settings must have those made explicit hashed inputs, or the cache lies.

5. **Resident, sticky workers + incrementality.** A worker keeps its sub-DAG's shapes
   resident across recomputes (like an Onshape session). An interactive edit re-sends only
   the cut-edge shapes that actually changed — not the document. Incrementality keeps a
   drag interactive.

**Summary:** sync-without-chattiness = coarse partition + few cut edges + shared-memory
binary-BRep bulk transfer + content-hash cache + resident workers doing incremental
updates. Chattiness is avoided by construction (edge granularity, not query granularity);
cost is bounded by cut size, which partitioning controls.

## 4. Hard parts (FreeCAD-specific)

1. **`execute()` is not pure — the crux.** `Document::recompute()` is currently serial,
   single-threaded, in-process, and many `execute()` implementations read other objects,
   mutate siblings, touch `App::Application` singletons, or call back into the document.
   Relocating recompute requires a **purity contract**: `execute()` reads only (its own
   properties + its input links' results) and writes only (its own outputs). Auditing and
   refactoring features to honor it — starting with Part/PartDesign primitives, flagging
   each type `relocatable` — is the bulk of the work. This contract *also* guarantees
   deterministic results, which the AI interface needs (§6). One audit, two payoffs.

   *Cost realism + the cheaper rung:* the audit is realistically **years**, touching
   hundreds of `execute()` implementations including Python addons outside this repo. There
   is a much cheaper intermediate that needs **no purity contract at all**:
   **process-per-document** — the document is already the consistency unit, so whole-document
   workers give crash isolation, GIL escape across documents, and force the serialization
   plumbing into existence. With `App::Link`-based assemblies (this fork's own architecture),
   that is already real parallelism: each linked part-document recomputes in its own
   process; the assembly joins. Do process-per-document first; treat intra-document DAG
   partitioning as the later refinement, not the main event.

2. **The GIL forces the process boundary for Python features.** Pure C++ features
   parallelize with threads (TBB/`std::thread`) — cheap, no serialization. Python-scripted
   features cannot run truly parallel in one interpreter, so they need **separate processes**
   (or subinterpreters / free-threaded Python 3.13+). This means Python + the module must be
   **co-resident in the worker** — which is compatible with running that worker as a
   Pyodide/WASM sandbox (see RoadMap workstream 3).

3. **Topological-naming state is shared mutable document state.** A `TopoShape`'s element
   map + `StringHasher` IDs must travel *with* the shape across the boundary, but
   `StringHasher` is a document-global dedup table. Options: ship the relevant hash entries
   alongside the shape, or make the hasher **content-addressed** so IDs are stable without a
   shared table (favored). This is the one genuinely shared piece of state in an otherwise
   clean data-flow — do not underestimate it.

4. **Expression / property cross-edges.** The expression engine adds dependencies beyond the
   shape DAG (object B's parameter references object A's property value). These are cheap
   scalars but are extra cut edges — fold them into each object's serialized *output state*,
   not just its shape.

5. **Undo/transactions across resident workers.** Transactions today are in-process property
   snapshots; resident worker state breaks that. Two options: **command replay** against
   workers (requires determinism — which the purity contract provides) or **worker-state
   snapshotting** (expensive). Decide early: the choice constrains the protocol design
   (replay favors a command-log protocol, which also suits collaboration later). Do not
   defer this past the protocol-v0 design.

## 5. Build order

Prove each layer before adding the next; never debug scheduling and serialization at once.
Each step must deliver standalone value — later steps proceed only on evidence from earlier
ones, not on momentum.

1. **v0 semantic protocol / MCP surface over the *existing* Python API** — the cheapest item
   here and the highest-information one. No headless split, no refactor: an in-process MCP
   server exposing schema'd operations, semantic queries, and structured errors. Real agent
   usage teaches what the protocol needs *before* the expensive machinery commits to it.
2. **Content-hash memoization in-process** — standalone win (skip unchanged recompute) and
   the cache substrate for everything later.
3. **Process-per-document** — whole-document workers; no purity contract needed (see §4.1).
   Crash isolation, GIL escape, assembly-level parallelism via `App::Link`ed documents, and
   the serialization plumbing (binary BRep, element map, §4.3) gets built here.
4. **Purity contract + `relocatable` audit** — start with Part/PartDesign. Long-running;
   overlaps with everything after it.
5. **In-process parallel scheduler** — frontier scheduler over the topo sort + thread pool,
   C++ features only. Prove parallel result == serial result with **zero serialization**.
6. **Intra-document process boundary** — sub-DAG workers; shared-memory binary-BRep for cut
   edges; resident sticky workers; incremental updates.
7. **Element-map / `StringHasher` serialization** hardening (content-addressed hasher) —
   begins in step 3, completes here.
8. In parallel from step 1: route GUI **mutations** through the protocol over an in-process
   transport (§1 staging), then migrate the GUI to a true client tier by tier.

## 6. The unified semantic protocol (= client boundary = distribution boundary = AI interface)

Do **not** build a separate "AI API." Design Boundary A's document-operation protocol well
and the GUI, scripts, remote clients, and AI agents all consume the *same* semantic
interface — with one honest qualifier: the GUI additionally needs the low-latency local
interaction tier described in §1 (client-side sketch solving, local preview); the semantic
protocol carries its *operations and commits*, not its 60 Hz feedback loop. The properties
that make it a good headless protocol are the same ones that make it AI-friendly:

- **Semantic, schema'd, versioned operations.** Every operation has a typed, self-describing
  schema (JSON Schema). Agents discover the operation catalog and document state by
  introspection — not by scraping FreeCAD's huge, under-documented Python API. Expose over
  **MCP** (operations = tools; document / DAG / objects = resources) so any agent framework
  can drive the engine.
- **Cheap semantic state queries.** Read state without downloading meshes or rendering:
  list objects, get the DAG, get properties, get measurements / bbox / mass-props, get
  recompute status and errors. Perception via *facts*, with on-demand headless render only
  when the agent genuinely needs to "see."
- **Structured errors, not crashes/tracebacks.** A failed boolean returns a structured,
  reason-carrying result the agent can correct. Out-of-process crash isolation is what turns
  a kernel segfault into "operation failed" instead of a dead session — crash isolation is
  itself an AI-friendliness feature.
- **Transactions + determinism.** Expose do/undo/rollback boundaries (build on
  `AutoTransaction`) for the agent try/refine loop, and make results deterministic — exactly
  what the `execute()` purity contract + content-hashing already provide.
- **Sandboxed codegen surface.** The most powerful agent mode is "write a script, run it,
  get structured results." Provide a sandboxed scripting endpoint (the Pyodide/WASM sandbox)
  with structured I/O. (Onshape's FeatureScript is AI-friendly for this reason: declarative,
  sandboxed, textual.)

## 7. Convergence

All four goals — headless engine, parallel recompute, distributed/cloud scale, AI-native
automation — collapse onto **one deliverable: a semantic, schema'd, transactional,
deterministic document-operation protocol with resident server-side state.** That protocol
is the client boundary, the distribution boundary, and the AI interface at once. The
parallel-recompute machinery lives underneath it and stays invisible to every consumer.

Which is exactly why the build order starts with the protocol (§5 step 1): it is the most
important interface in the design, and it can be battle-tested cheaply — over the existing
in-process API, driven by real agent usage — before any of the expensive machinery is built
underneath it.
