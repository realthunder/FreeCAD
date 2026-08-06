# Multi-document serving — one backend, several documents, one wire

Status: **stages 3a–3c implemented** (§8) — the server is grouped by document, the gates
are re-keyed, and the wire carries document names: hello `doc`/`client`, the `docs`
listing, `switch`, the join gate, teardown re-homing. The viewer (3d) and the token (3e)
are still to come. Stage 2 of [HeadlessServe.md](./HeadlessServe.md) deliberately scoped
serving to one source, one document, one port (its §5), and this document is the decision
that reversed that scoping.

Companions: [HeadlessServe.md](./HeadlessServe.md) (the view-less source this multiplies),
[SceneStreaming.md](./SceneStreaming.md) (the wire this extends),
[ThinClient.md](./ThinClient.md) §4.2 (the control channel that gains document routing),
[ComputeBoundaries.md](./ComputeBoundaries.md) §1 (the process-per-session model that bounds
what "user" can mean here).

---

## 1. Where stage 2 left it

`SceneStreamServer` is a singleton with exactly one of everything a document needs
(`SceneServer.cpp`): one retained payload + version + root spans (~147-158), one 32-entry
delta history (~170), one session id (~175), one `publisher` latch (~176), one content-keyed
blob store with its retire sets (~254-260), one level-job queue (~328-350), and one slot each
for the pick, control and work handlers (~517-519). A `Conn` (~540) tracks only the scene
version it holds — there is no notion of *which* scene, because there is only one.

That is not merely a missing feature; it is why a second `Gui.serveDocument()` today is
actively destructive rather than refused. The 2026-08-06 review of the stage-2 code
established the failure shape: the second source's `installHandlers()` overwrites the
singleton's pick/control/work slots while the `beginPublish()` first-claim latch keeps the
second publisher off the wire — so viewers keep seeing document 1 while their clicks are
ray-picked against document 2, and document 1 stops hearing about finished level builds.
`SceneServeSource::renderProperties()` returns the first entry of a pointer-ordered map, and
a `serve()` that fails after `start(port)` leaves a listener up that no code can stop, which
flips the process-wide coarse-tessellation gate with no publisher behind it. The redesign
below is written so each of those becomes structurally impossible, not individually patched.

## 2. Decisions

Settled with the user, 2026-08-06:

- **One port, a document key on the wire.** One listener multiplexes every served document.
  The alternative — one port per document — needs no protocol change, but costs a tunnel per
  document on the real serving rig, forfeits cross-document blob sharing, and gives viewers
  no way to discover or switch documents.
- **Live switching.** A viewer picks a document at hello (`?doc=` in the page URL) *and* can
  switch on an open connection; the viewer grows a document list in its menu. A switch is
  versioning-wise a fresh join (§4).
- **Documents are named by their `App::Document` internal name.** Stable within a session,
  unique per process, human-typable in a URL. An opaque serve id would decouple the wire
  from document naming but forces a listing round-trip before anything is usable.
- **A backend is one collaborative room, not a multi-tenant host** (§7). "User" inside one
  backend means identity — a client label, an optional shared token at the door — never
  isolation. Isolation is a process boundary and belongs to the gateway layer above.

## 3. The server: document groups

`Private` splits into what is per-document and what is genuinely per-process. Everything
per-document moves into a `DocGroup`, held in a `std::map<std::string, DocGroup>` keyed by
document name:

- payload, version, `handedOut`, root spans, delta `history`, `session`;
- the `publisher` latch — `beginPublish(publisher)` becomes `beginPublish(doc, publisher)`,
  first-claim *per document*;
- the pick / control / work handler slots — installed by that document's source, so a second
  source no longer steals the first one's;
- the level-job bookkeeping (`levelAsked`, jobs, `levelsDone`): rung demand is driven by the
  viewers of one document's scene;
- the blob **key sets** (`pendingKeys`/`currentKeys`/`previousKeys`): which keys a publish
  cycle references is a per-document fact.

What stays global, and why:

- the listener, the accept loop, `conns`;
- the **blob store itself**. Blobs are content-keyed and immutable, so sharing them across
  documents is free and correct — the same TShape appearing in two documents is stored and
  streamed once. Retirement changes meaning: a key retires when *no* group's current or
  previous set references it (union across groups), not when one document's publish stops
  naming it;
- the level worker threads (the queue entries carry their group);
- the bundle stamp cache, the dump/log collectors (already per-request).

A server with zero groups is a valid state: it listens, answers the document list (§4) with
an empty set, and waits. That retroactively fixes the orphaned-listener hazard — a `serve()`
whose source construction fails leaves a server that is merely *empty*, and the tessellation
gate asks about the document, not the listener (§5).

## 4. The wire

The hello grows three fields, all optional:

```json
{"cmd": "hello", "build": "…", "snapshot": 33,
 "doc": "Fountain", "client": "lei-phone", "token": "…"}
```

- `doc` — the document to join. Absent: the **default document**, which is the first one
  served and still alive; every existing viewer and probe therefore keeps working unchanged.
  Unknown name: a text-frame error and the connection stays alive, joined to nothing, so the
  viewer can show the list instead of dying.
- `client` — a display label for this connection. Lands in `Conn`, the decisions journal,
  and later presence/attribution. Never parsed, never trusted.
- `token` — checked against a server-side shared secret when one is configured (parameter
  group, `FC_SERVE_TOKEN` override). When set, a hello without the matching token is
  refused before any scene bytes move. When unset, behavior is exactly today's.

New text-channel verbs, symmetric JSON:

- `{"cmd":"docs"}` → `{"cmd":"docs","list":[{"name":…,"label":…,"objects":n}…],"default":…}`
  — also pushed unsolicited to every connection when a document is served or unserved, which
  is what the viewer's menu redraws from.
- `{"cmd":"switch","doc":…}` — leave the current group, join the new one. A switch resets
  the connection's `sent` version to 0, so the next push is the new document's full
  snapshot: versioning-wise it *is* a fresh hello, reusing the join path rather than growing
  a parallel one. Delta history, sessions and versions are per-group, so nothing about one
  document's continuity leaks into another's.

Binary frames gain a document prefix **nowhere**: a connection is joined to exactly one
group at a time, so the existing frames stay byte-identical. Picks, control requests, level
demand and dump uploads are attributed to the connection's current group server-side.

Every join — hello or switch — goes through one gate:

```
bool joinDocument(Conn &, const std::string &name)
```

which today checks only that the group exists (and the token, if configured). This is
deliberately the single choke point a later per-connection ACL would occupy (§7); handlers
never look up groups by name themselves.

## 5. Sources and gates

`Gui.serveDocument(doc, port)` may now be called once per document; the first call with a
non-zero port starts the listener, later calls add groups. `SceneServeSource` changes:

- `installHandlers()` registers with the server *keyed by document name* — into its own
  group, not the process-wide slots. The half-hijack in §1 becomes unrepresentable.
- `renderProperties()` gains the document: `renderProperties(App::Document*)`, resolved
  through the group. The pointer-ordered-map arbitrariness goes away with the map.
- Teardown on document close (already implemented, `565a2bab12`) now also removes the group,
  which pushes an updated `docs` list and moves the connections of that group to the default
  document (or to nothing, with the error text, when it was the last).

The consumers re-key from "is the server running" to "is *this* document served":

- `PartGui::coarseTessellationLevel()` / `sceneServed()` (`MeshLevelSource.cpp:259`) asks
  for the source of the document that owns the shape it is about to tessellate. The env-var
  arm stays (it names a port bound only at first publish); the `running()` arm becomes
  per-document. This also removes the mixed-process regression where one served document
  froze the refine hooks of every desktop view in the process.
- `SceneControl`'s `view3d` subject resolves the container of the document named in the
  request's connection group — not "the active view, else some source". In a mixed
  GUI+headless process the rule is: **the publisher that owns the document's stream owns
  its container**; a desktop view of the same document reads, but remote edits land on the
  serving container so the republish hook always fires.

## 6. The viewer

- `?doc=<name>` in the page URL becomes the hello's `doc`.
- The viewer menu grows a document section, drawn from the `docs` push; picking an entry
  sends `switch`. The scene model, selection mirror and level state all reset exactly as on
  reconnect — the switch path reuses the reconnect machinery, it does not duplicate it.
- `?client=` and `?token=` pass through to the hello. No UI beyond that in this pass.

## 7. Users: a room, not a tenancy — and how a cloud service scales it

A backend process is one trust domain. Python sees every document; so do expressions and
macros. `Gui::Selection` is process-global *and published* — one viewer's pick changes what
every viewer of that document sees, which is already true today with two viewers. Recompute
blocks the process; undo stacks, preferences, render overrides and the blob store are
shared; a kernel crash takes everyone down. No wire-level concept can wall users off from
each other inside that; pretending otherwise would be a security boundary made of paint.

So inside one backend, "users" are **members of a collaborative room**: the `client` label
names them, the `token` gates the door, and the join gate (§4) is where a later
per-connection document ACL would sit — enforceable against *viewers* (the control channel
is semantic and must stay free of code execution), meaningless against anyone with Python.

**Isolation between users is the gateway's job**, one backend process per user or per shared
project — the same unit as [ComputeBoundaries.md](./ComputeBoundaries.md) §1's "one server
per document/session". For a cloud service the shape is conventional and proven
(Onshape-style geometry sessions, JupyterHub-style kernel pools):

- a **gateway** terminates TLS and auth, maps user → backend, and proxies the scene
  WebSocket unchanged — the wire in §4 needs nothing from it;
- a **session manager** spawns backends on demand, reaps them on idle, and bin-packs them
  per box. The stage-2 result is what makes the economics work: an idle serving backend
  costs **~0% CPU and no GPU ever** (no GL library in the process), so density is bounded
  by memory, and memory is dominated by the model, not the runtime;
- **hibernation is a save**: an idle session serializes to `.FCStd` (plus the blob store,
  content-keyed and re-derivable), the process dies, and reconnect resurrects it — cold
  start = spawn + load, hidden by the retained payload a rejoining viewer gets from the
  gateway cache or a warm pool;
- documents-per-process (this design) sets the granularity: one user's *n* documents are one
  process, not *n*.

None of that is built here; the point is that this wire design is already the thing such a
service would proxy, and the room/tenancy split above is what keeps the two layers from
needing to know about each other.

### 7.1 Why process-per-session scales

The recurring objection to one-process-per-user is process weight. The measured cost
profile answers it:

- **CPU is bursty, so it oversubscribes.** An idle serving backend measures ~0% CPU
  (stage 1/2); cycles exist only during recompute and edits. Sessions pack onto cores at
  ratios set by duty cycle, not session count.
- **No rendering tier exists.** Viewers rasterize the published scene themselves, so the
  server side scales like a document database, not like cloud gaming — no GPU fleet, and
  no per-viewer server cost beyond a socket and a delta cursor.
- **Density is therefore RAM-bound**, and RAM is dominated by the model (BRep,
  tessellation, render caches), not the runtime tax of Python + OCCT + Coin. A
  memory-dense box carries on the order of a hundred light sessions or a handful of
  giant-assembly ones; the session manager bin-packs by memory headroom.
- **Idle sessions need not exist.** Durable truth is the `.FCStd` plus the content-keyed
  (re-derivable) blob store, so hibernation is a save-and-kill and resurrection is
  spawn-and-load. The gateway can serve a rejoining viewer the cached retained payload
  while the backend cold-starts behind it — which is also why document-load/import
  performance is cloud economics, not just desktop comfort. A warm pool or fork zygote
  (sharing runtime pages copy-on-write across a box's sessions) hides the spawn half.
- **Sessions are portable**, because their state is a file: drain, move, resurrect
  anywhere; boxes are cattle; the gateway just re-routes the proxy.
- **This design sets the granularity**: one user's *n* documents are one process, not *n*.

The ceiling of the model is that a session's recompute burst is trapped on its box and a
big model's memory in one process. That ceiling is lifted by exactly the
[ComputeBoundaries.md](./ComputeBoundaries.md) split this project already plans: a
RAM-light session tier that holds documents and streams scenes, and an elastic geometry
worker tier that absorbs recompute bursts wherever capacity is — at which point session
count and compute scale independently. Onshape (per-session geometry servers) and
JupyterHub (per-user kernels) are the proven precedents; the structural advantage here is
the absent rendering tier.

## 8. Staging

Each stage lands alone, with the single-document behavior as its own oracle — the stage-2c
diff harness (`fcscenediff`) and the probe set in `~/works/sw/fcad-probes/` all keep passing
untouched at every step, since an unadorned hello must keep meaning "the default document".

- **3a — groups, one group.** Restructure `Private` around `DocGroup` with the map capped at
  one entry and no wire change. Pure refactor; every existing probe is the regression test.
- **3b — the gates re-key.** `beginPublish(doc, …)`, per-group handlers,
  `renderProperties(doc)`, `sceneServed(doc)`, the `SceneControl` routing rule. The
  lifecycle probes (`lifecycle_scene.py`/`lifecycle_probe.py`) extend to two documents:
  serve both, delete from one, close one — the other's viewers must see nothing change.
- **3c — the wire.** `doc`/`client` in the hello, `docs`, `switch`, the join gate, group
  teardown pushing the list. Verify with a two-connection probe: each joined to a different
  document, each seeing only its own manifest names; then a switch mid-connection.
- **3d — the viewer.** `?doc=`, the menu section, switch-as-reconnect. Verified on real
  Chrome per the established recipe (headless masks gesture and reload behavior).
- **3e — the token.** Last, because it is the only stage that can refuse a connection: a
  wrong-token hello gets no scene bytes; an unset secret changes nothing.

## 9. Non-goals

- **Tenancy inside a backend** — §7; the gateway owns isolation.
- **Per-viewer selection.** Selection stays a property of the room. The client-side
  selection design in the thin-client queue is the right refinement and is orthogonal.
- **The gateway itself.** Nothing in this design may depend on one existing.
- **A headless image renderer.** Same separation as HeadlessServe.md §5 — serving publishes
  scenes; it does not make pictures.
