# Thin Client — DOM UI over the WASM viewer

Status: design / direction. Not yet implemented.
Companions: [RoadMap.md](./RoadMap.md) (workstreams 1–5), [ComputeBoundaries.md](./ComputeBoundaries.md)
(the two-tier client + semantic protocol), [ViewerUIResearch.md](./ViewerUIResearch.md)
(UI-framework survey → the DOM-overlay conclusion this doc builds on),
[SceneStreaming.md](./SceneStreaming.md) (how the scene itself reaches this client
at big-model scale — the content-addressed manifest tree and delta publishing).

This document specifies how to grow the current bgfx/WASM **viewer** into a **thin CAD
client**: a browser/mobile/desktop app whose 3D surface is the bgfx canvas and whose UI
chrome is DOM, driven by a semantic operation channel to the headless-ish backend. The
concrete first deliverable is a **property inspector that pops up when an object is
selected**, then edits that flow back through recompute.

The reference experience is **Shapr3D** (iPad-first parametric CAD): a near-empty canvas
where the model is the hero, a small icon tool rail, a **selection-contextual action bar**,
floating inspectors anchored to the selection, and direct on-model manipulation — all
touch-first but equally good with a mouse. We are not cloning it; we are borrowing its
interaction grammar.

---

## 1. Why this shape (and why now)

[ViewerUIResearch.md](./ViewerUIResearch.md) already settled the framework question: **every
serious web CAD product (Figma, AutoCAD Web, Onshape) uses a native-speed WASM/WebGL canvas
+ browser DOM for all UI chrome.** Nobody renders inspector panels inside the GL canvas —
the mobile soft keyboard, IME/CJK, accessibility, scrolling physics, clipboard and file
dialogs are all free and correct in DOM and are chronic reimplementation projects in-canvas.

So the division of labour is fixed:

- **DOM** — every panel, toolbar, menu, form field, text input, dialog. This is the "thin
  client UI."
- **bgfx canvas** — the 3D scene *and* only the adornments that are locked to 3D space:
  selection highlight (already streamed), preselection, and future drag handles / dimensions
  / snap markers. The existing NaviCube and axis cross are streamed **overlays** and stay in
  the canvas.

"bgfx-embedded UI if appropriate" (from the request) resolves to exactly that in-scene set —
not panels. If we ever want an in-canvas engineer HUD, ImGui-on-bgfx is the escalation path
from ViewerUIResearch §(a); it is **not** on this doc's critical path.

This also lands a roadmap item early and cheaply. [ComputeBoundaries.md §1](./ComputeBoundaries.md)
calls for a **two-tier client**: a low-latency *local* tier (selection, orbit, previews) over
a *semantic operation* tier (create feature, **set property**, commit). The viewer already
_is_ the local tier — orbit, pick, hover, selection all resolve client-side with zero
round-trips (`main.cpp` §4 below). Adding a property inspector that reads/writes over a
control channel is the first real slice of the semantic tier, validated by real interaction
before any headless-split machinery is committed underneath it.

---

## 2. Current state (what we build on)

Anchored to the code; fuller map in the git history of this doc's companion handoffs.

**Front-end** is deliberately minimal (`src/Gui/Renderer/wasm/shell.html`,
`src/Gui/Renderer/wasm/main.cpp`, ~2160 lines): one full-viewport `<canvas>`, a 3px loading
bar, and an optional debug HUD injected via `EM_JS`. All "widgets" today (NaviCube, axis
cross, fps) are **bgfx-drawn**, not DOM. There is **no DOM UI layer** and no JS build step —
UI strings are inlined in `main.cpp`.

**Transport** (`src/Gui/Renderer/SceneServer.{h,cpp}`): a hand-rolled POSIX WebSocket server
at `ws://<host>:<port>/scene` (default port **8077**, `FC_BGFX_SERVE_SCENE`), with an HTTP
long-poll fallback `GET /scene?v=<n>`. The backend is ordinary desktop FreeCAD running the
bgfx renderer; it **publishes geometry snapshots, not pixels**.

**Payload** (`src/Gui/Renderer/SceneDump.h`): a versioned little-endian `SceneSnapshot`
carrying the whole render feed — `scene` draw calls, `selections`, `highlight`, viewport
`overlays` (NaviCube = id 5, axis cross, editing graph), all per-frame configs (AO, PBR,
water, bloom, highlight styling…), and camera/viewport matrices. **One inbound wire message
type today: the binary snapshot** (`main.cpp:onWsMessage → applyScenePayload`). Text frames
already carry a small JSON control vocabulary both ways (`hello`/`resync` up;
`reload`/`config` down; `SceneServer::handleMessage` + `pendingText`) — the property channel
extends this lane with id-correlated request/response (§5).

**Object identity — the crux.** Each `DrawCall` carries `uint64_t objectKey` = a **content
hash of the scene-graph node path** (`Renderer.h:826`, assigned at
`SoFCRendererBridge.cpp:776`), plus part indices and bboxes. **There are no document / object
/ sub-element name strings in the payload.** The browser can group and highlight draws by the
opaque hash, but **cannot name what it clicked** — the single biggest gap for a property UI.

**Selection / picking** (`main.cpp` §4) is **fully client-side**: `pickScene` raycasts the
streamed meshes, resolving Face/Edge/Vertex; hover preselect and selection are local and
instant (`selectAt`, `rebuildSelection`, client selection group `kClientSelId`). The browser
knows `{objectKey, kind, part}` — enough to highlight, not to name.

**Back-channel is defined but dormant.** `SceneServer` can receive masked `'P'`/`'B'`
world-ray pick frames → `dispatchPick` → `View3DInventorViewer::pickAndSelect`
(`:3783`), which resolves `ViewProvider` + `subname` and updates `Gui::Selection`. **But
`main.cpp` never sends** — the WASM client contains no `emscripten_websocket_send`. Camera and
selection never reach the desktop today.

**Property access** exists only desktop-side: `Render_*` dynamic ViewProvider properties driven
by `src/Gui/TaskRenderSettings.cpp` (a hand-written property→widget table — the model for our
generic schema, §4), plus the MCP console (`src/Ext/freecad/mcp_console/server.py`,
`http://127.0.0.1:8765/mcp`, `run_python`/`search_api`) for introspection. **No property API
is exposed to the browser.**

Net: we have an excellent local renderer + a snapshot stream, and we are missing exactly two
things for the first deliverable — **object identity in the stream** and a **semantic control
channel**.

---

## 3. Target architecture

```
        Browser / mobile / Tauri shell
 ┌───────────────────────────────────────────────┐
 │  DOM UI layer  (TS, its own bundle)            │
 │   • property inspector   • tool rail           │
 │   • contextual action bar• context/radial menu │
 │   • numeric fields (native soft-keyboard/IME)  │
 │        ▲ selection events     │ operations     │
 │        │ (local, instant)     ▼ (semantic)     │
 │  ┌─────┴───────────┐   ┌───────────────────┐   │
 │  │  WASM viewer    │   │  Control client   │   │
 │  │  (bgfx canvas)  │   │  (WS text frames) │   │
 │  │  orbit/pick/sel │   │                   │   │
 │  └─────┬───────────┘   └─────────┬─────────┘   │
 └────────┼───────────────────────┼──────────────┘
   /scene  │ binary snapshots       │ JSON control   ws://…:8077/scene
          ▼ (server→client)         ▼ (both ways)
 ┌───────────────────────────────────────────────┐
 │  Desktop FreeCAD backend (bgfx renderer)       │
 │   SceneServer  ── snapshot publish             │
 │               └─ control dispatch (main thread)│
 │   Document / ViewProvider / Selection / OCCT   │
 └───────────────────────────────────────────────┘
```

Three cooperating pieces in the browser, **one transport**:

1. **WASM viewer** (`main.cpp`, unchanged in role) — owns the bgfx canvas, the `/scene`
   snapshot socket, and the *local interaction tier*: orbit, zoom, hover, pick, selection
   highlight. Latency-critical, zero round-trips. On a selection change it emits a JS
   **`fc:selection`** event carrying the **resolved identity** (from the new identity table,
   §4.1) — not just the opaque hash.

2. **DOM UI layer** (new, a real TS bundle) — listens for `fc:selection`, renders the
   inspector and chrome, and issues **operations** (get/set property, run modeling op) over
   the control client. This is where all Shapr3D-style chrome lives.

3. **Control client** (new, thin) — a JSON message client over **WebSocket text frames on the
   same `/scene` connection** (the text lane already exists for `hello`/`resync`/`reload`, so
   no second socket, no new port, no extra CORS/TLS story). Carries the semantic tier:
   `getProperties`, `setProperty`, later `runOp` / `commit`.

Why text frames on the existing socket rather than a separate connection or the MCP endpoint:
the `/scene` WS already exists, is already secure-context-clean through the tunnels, and its
binary vs text frame split gives us a free, in-order, bidirectional control lane next to the
snapshot lane. The MCP server (`:8765`) stays the *AI/automation* face of the same operations
(RoadMap workstream 5) — one operation catalog, two front doors, later unified.

**Threading constraint (non-negotiable):** control messages touch the `Document`,
`ViewProvider`s and `Gui::Selection`, which are **main-thread only**. `SceneServer` runs its
socket on a worker thread. The control dispatch must **marshal onto the GUI/main thread**
(post a queued event, mirror the existing pick-handler path and the `dfd144024b` queued-
notification fix) and reply asynchronously. Never touch the document from the socket thread.

**Framework choice (decided 2026-07-31): SolidJS + TypeScript, built with Vite.** The DOM
layer's state is a handful of reactive streams (selection, property rows, pending acks,
connection state) and Solid's fine-grained signals update exactly the DOM nodes a change
touches — no virtual-DOM re-render, ~7 KB runtime, top-of-benchmark update cost, which
matters on phones where the WASM viewer owns most of the frame budget. The trade-off
accepted: Solid's compiler is the framework (component code ports nowhere), and there is no
React-ecosystem escape hatch — bespoke CAD chrome doesn't need one. Vite provides the dev
server/HMR lane and emits a static bundle `wasm-viewer.sh` serves like any other file; one
`package.json` under `src/Gui/Renderer/web/`. The control-client and event contract below
stay framework-agnostic on purpose, and the same bundle serves a future **Tauri**
desktop/mobile shell (ViewerUIResearch §(b)). This also means **moving UI out of `main.cpp`
`EM_JS` strings into the `web/` bundle**, with a tiny `cwrap`/embind bridge for the few calls
the DOM makes into WASM (e.g. "project this 3D point to a screen pixel" for anchoring a
popover).

**Why not Qt Quick/QML (considered, rejected 2026-08-01):** QML-in-WASM fights everything
this design optimizes for. (a) *Payload* — Qt6+QML compiled to WASM is ~20–30 MB even
aggressively trimmed, against ~7.5 KB gzipped for the Solid bundle; first paint over a
tunneled mobile link is a headline requirement (§2), and a Qt runtime download sinks it.
(b) *Canvas ownership* — QML draws through its own scene graph into a GL context and wants
the Emscripten main loop; our canvas and frame pacing belong to bgfx. Embedding QML means a
second canvas composited over the viewport or ceding the render loop — both conflict with
the viewer's architecture. (c) *Text input* — QML in the browser renders its controls
in-canvas, reimplementing input handling: mobile soft keyboards, IME (the CJK contract,
§4.2), clipboard, and accessibility are precisely where that is weakest, and native DOM
inputs give them for free (see Touch policy below). (d) *Debuggability* — DOM is live in
devtools (our devtools-less-phone breadcrumbs build on that); a canvas UI is opaque.
(e) *No reuse dividend* — the desktop GUI is Qt Widgets, not QML, so QML would be a third
UI technology with nothing to share. QML becomes interesting only if we ship a fully native
Qt mobile client sharing UI with a QML desktop — neither is on the roadmap; the native-shell
path here is Tauri wrapping this same DOM bundle.

**Touch policy:** gestures are hand-rolled over the standard Pointer Events API
(`setPointerCapture`); sheet detents and scroll physics are CSS (`scroll-snap`,
`overscroll-behavior: contain`, `env(safe-area-inset-*)`); text/number entry is always a
native DOM input (`inputmode`, native pickers). No gesture or motion library. Every
interactive element declares `touch-action` explicitly so the canvas-vs-panel gesture split
is auditable in one grep.

**Prior art — Onshape (verified from their engineering blog + forums, 2026-07):** a
decade-scale existence proof that no client kernel is needed. Their clients are "not thin
clients, but incomplete CAD systems" — custom WebGL/OpenGL renderers receiving tessellation
only; the ~72 MB iOS / ~142 MB Android apps are fully native rendering clients with zero
offline mode; every kernel operation (booleans, fillets, tessellation, assembly solve, and
even D-Cubed sketch solving) runs server-side, with progressive tessellation refined after
interaction stops. Their known weakness — interactive edits feel round-trip latency on slow
links — is exactly what our local tier (client-side picking today; ghost previews and a
client sketch solver later, per ComputeBoundaries §1) is positioned to improve on.

---

## 4. First deliverable — property inspector on selection

Goal: **click/tap any object (or a face/edge) → a floating DOM inspector appears showing its
properties; edit a value → the model recomputes and the view updates.** Split into a
read-only slice (4.A) and an editable slice (4.B) so each ships standalone value.

### 4.1 Prerequisite — object identity in the stream

Add a small **identity table** to `SceneSnapshot`: `objectKey → { doc, name, label, type }`
(internal name, user `Label`, and the `DocumentObject` type id). It is tiny (one row per
distinct object, not per draw call), populated where `objectKey` is already computed
(`SoFCRendererBridge.cpp:776`, where the traversing entry's `ViewProvider`/`DocumentObject`
is in hand — confirm reachability there; if not, thread it from the bridge's object context).
Bump `SceneDump kVersion` and serialize it beside the existing feeds (mirror the overlay
serialization); the WASM viewer parses it into a `objectKey → identity` map.

Payoff: the client can now **name any pick instantly and locally** (no round-trip for the
identity), which keeps selection in the fast local tier while the property *data* comes over
the semantic tier. Sub-element identity (`Face3`, `Edge1`, …) is reconstructed on the client
from the pick `kind` + `part` index it already computes (`partForHit`, `main.cpp:1048`) — the
FreeCAD subname convention (`ObjName.Face3`) is `name + "." + subelement`.

### 4.2 Control-channel messages (v0)

JSON text frames. Request/response correlated by `id`. Minimal v0:

```jsonc
// client → server
{ "id": 1, "op": "getProperties",
  "doc": "Unnamed", "obj": "Box", "sub": "Face3",   // sub optional
  "scope": ["object", "view"] }                      // App props + ViewObject props

// server → client
{ "id": 1, "ok": true, "props": [
  { "name":"Length", "group":"Box", "type":"Length",
    "value": 10.0, "unit":"mm", "readonly": false,
    "constraints": { "min":0, "max":null, "step":1 } },
  { "name":"Placement", "group":"Base", "type":"Placement", "value": { … } },
  { "name":"Render_Metallic", "group":"Render", "type":"Float",
    "value":0.9, "constraints":{ "min":0,"max":1,"step":0.05 } },
  { "name":"ShapeColor", "group":"Object", "type":"Color", "value":"#bfbfc7" }
]}

// client → server  (edit)
{ "id": 2, "op": "setProperty",
  "doc":"Unnamed", "obj":"Box", "target":"object",
  "name":"Length", "value": 25.0 }

// server → client  (ack; the recompute + republished snapshot updates the view)
{ "id": 2, "ok": true, "recomputed": true }

// server → client  (unsolicited, structured — never a raw traceback)
{ "op":"error", "ref":2, "code":"ConstraintViolation",
  "message":"Length must be > 0" }
```

**Forward hook — preview/commit + supersedes.** When drag-driven ops arrive (§5), they reuse
this channel at 5–10 Hz with a `"preview": true` flag and a final committed call. Two
semantics to bake in early so the protocol doesn't need a breaking change: (a) a request may
carry `"gesture": <id>`; a newer preview in the same gesture **supersedes** any queued older
one server-side (the dispatcher keeps only the latest un-run preview per gesture — a slow
recompute never builds a backlog); (b) previews run inside the gesture's single open
transaction, and only the commit closes it. `setProperty`/`getProperties` v0 ignores both
fields; the dispatcher just reserves the queue-per-gesture slot.

**Non-ASCII identifiers are first-class.** This fork allows non-ASCII *internal* names for
documents, objects and (dynamic) properties — not just labels. Every layer of the channel
treats identifiers as opaque UTF-8: length-prefixed strings in the snapshot, UTF-8 JSON on
the wire, no `[A-Za-z0-9_]` assumptions anywhere (validation is by lookup, never by lexical
shape). The e2e suite includes a CJK-named document/object/property pass.

The **property descriptor** is the serializable generalization of the hand-written table in
`TaskRenderSettings.cpp:331`: `{ name, group, type, value, readonly, hidden, unit?,
constraints?{min,max,step}, enums?[] }`. The `type` set maps 1:1 onto DOM controls:

| FreeCAD property | descriptor `type` | DOM control |
|---|---|---|
| `PropertyBool` | `Bool` | toggle / checkbox |
| `PropertyInteger`, `PropertyFloat`(+`Constraint`) | `Int`/`Float` | number field + optional slider (min/max/step) |
| `PropertyLength`/`Quantity`/`Angle` | `Quantity` | number field + **unit** suffix |
| `PropertyEnumeration` | `Enum` | select / segmented control |
| `PropertyString` | `String` | text field (**native soft keyboard/IME** — the whole point of DOM) |
| `PropertyColor` | `Color` | color swatch/picker |
| `PropertyVector`/`Placement` | `Vector`/`Placement` | grouped x/y/z (+rotation) fields |
| `PropertyFileIncluded` | `File` | file picker (textures — reuses `Render_*Map` rows) |
| `PropertyLink*` | `Link` | read-only chip in v0 (picker later) |

Backend impl (`getProperties`): resolve `doc.getObject(obj)`, iterate
`obj.PropertiesList` + `obj.ViewObject.PropertiesList`, and for each read type id, group
(`getGroupOfProperty`), docstring, editor status (`getEditorMode`/hidden/readonly), and
constraints (`PropertyFloatConstraint::getConstraints`, enum lists). This is generic — one
serializer covers every object, and it subsumes the bespoke `Render_*` panel for free (those
props already carry their `Render` group, per the underscore-prefix convention). Reuse the
existing MCP `run_python` machinery only as scaffolding; the typed serializer is new but
small.

`setProperty`: on the main thread, wrap in an `App::AutoTransaction` (so undo/redo and the
try/refine loop work — RoadMap §5), set the value, `doc.recompute()`, return a structured
result. The renderer's existing `dirtyChanged → publish` path (`BGFXRenderer.cpp:6348`) then
streams the updated snapshot; the view refreshes with **no special-casing** — the parametric
loop, closed remotely. Constraint/validation failures return `{op:"error", code, message}`,
never a crash.

### 4.3 The inspector panel (DOM, Shapr3D-flavored)

- **Anchoring.** On `fc:selection`, the panel appears as a **floating card near the
  selection** (Shapr3D puts the inspector top-right / beside the selection, not docked). The
  WASM viewer already has the pick screen pixel; expose a `cwrap` "project point → pixel" so
  the card can tether to the object and follow orbit, with edge-avoidance so it never leaves
  the viewport. On narrow/mobile widths, collapse to a **bottom sheet** (thumb-reachable,
  swipe-to-dismiss).
- **Content.** Header = `Label` + type + a color swatch. Below it, two navigation controls
  (user-specified 2026-07-31): a **group drop-down** (`All` + the distinct `group` values:
  `Base`, `Object`, `Render`, …) selecting which property group is shown, and a **keyword
  box** doing QCompleter-style **live filtering** — each keystroke narrows the visible rows
  by case-insensitive substring match on property name (and label), with the matched
  substring highlighted. A non-empty keyword searches across *all* groups (the drop-down
  shows `All` while filtering); clearing it restores the selected group. Body = the
  filtered property rows. Multi-select → show the **common** subset.
- **Live editing.** Number fields commit on blur/Enter; sliders (constrained floats) preview
  continuously but only send `setProperty` on release (respect the semantic tier — no 60 Hz
  spam; local preview can come later via the interaction tier). Every field is a real DOM
  input → correct mobile keyboard, IME, copy/paste for free.
- **Touch ergonomics.** ≥44 px targets, generous spacing, steppers beside number fields for
  fingers, a compact numeric keypad popover for precise entry.

**Definition of done (Phase 1):** in the streamed demo scenes (`demo-lights.py`,
`demo-water.py`), tap the box/cylinder/water → inspector shows its App + ViewObject
properties including the `Render_*` set; change `Length` or `Render_Metallic` → the model
recomputes and the canvas updates. Verify desktop-GPU and browser (per the rendering-
verification workstream) and on a phone (bottom-sheet + soft keyboard).

---

## 5. Modeling operations — toolbars & context menus (Shapr3D grammar)

Once selection names objects and the control channel carries operations, modeling UI is
"more of the same op channel" with touch-first chrome. Design, not yet scheduled past a
skeleton:

- **Tool rail** — a slim DOM rail (left on desktop, bottom on mobile) of large icon buttons:
  *Sketch, Primitive, Extrude/Pocket, Fillet/Chamfer, Move/Transform, Boolean, Measure*.
  Icon-only; label on hover/long-press. Purely DOM.
- **Contextual action bar** — Shapr3D's signature: a compact bar that **changes with the
  selection**. Face selected → *Extrude · Offset · Move*; edge → *Fillet · Chamfer*; solid →
  *Move · Boolean · Pattern*; empty → *Sketch · Primitive*. Populated from selection type
  (already known locally) + an operation catalog the backend advertises (so it stays in sync
  with real capability, and the same catalog feeds MCP/AI). Appears near the selection.
- **Context / radial menu** — long-press (touch) or right-click (mouse) opens a DOM popover
  **anchored to the pick pixel** the client already has; radial layout for thumb reach.
- **Numeric commit** — an operation in progress (extrude distance, fillet radius) shows a DOM
  numeric field with unit + keypad; drag-on-model gives coarse value, the field gives
  precision. Commit = one `runOp` over the control channel → recompute → snapshot refresh.
- **Direct manipulation (later)** — push/pull handles are the one place we add **in-scene
  bgfx adornments**: draggers rendered in the canvas (like the selection highlight), with the
  live delta shown in a DOM readout. This is the interaction-tier upgrade (local preview) over
  the semantic commit; sketch-solver-client from ComputeBoundaries §1 lands here too.

Operation submission always follows the same pattern as `setProperty`: main-thread,
`AutoTransaction`-wrapped, structured result, view refresh via the existing publish path.
Undo/redo become two more control ops (`undo`/`redo`) mapping to `Document` transactions.

---

## 6. Phasing

Each phase ships standalone value; later phases proceed on evidence, per RoadMap discipline.

- **Phase 1 — identity + read-only inspector.** §4.1 identity table in the snapshot; §4.2
  `getProperties` control channel (text frames + main-thread marshalling); §4.3 read-only DOM
  inspector on selection; stand up the `web/` TS bundle and the `fc:selection` bridge.
  *Payoff: click anything, see its properties, on desktop + browser + phone.*
- **Phase 2 — editable properties.** `setProperty` + `AutoTransaction` + recompute; typed DOM
  editors for the descriptor table; structured errors; undo/redo ops.
  *Payoff: the parametric loop, driven from the browser.*
- **Phase 3 — modeling chrome.** Tool rail + selection-contextual action bar + context/radial
  menu; operation catalog advertised by the backend; a first real op end-to-end (e.g.
  primitive create + transform).
- **Phase 4 — direct manipulation + sketch tier.** In-scene bgfx handles with DOM numeric
  readout; client-side sketch constraint solver (interaction tier); previews before commit.
- **Cross-cutting:** Tauri shell reusing the bundle (desktop/mobile); session/auth for
  remote; the operation catalog unified with the MCP surface (RoadMap §5); delta snapshots
  (today the whole snapshot re-sends — fine at demo scale, revisit for large models).

---

## 7. Risks & open questions

- **Identity reachability at the bridge.** Confirm the `DocumentObject`/`ViewProvider` is in
  scope at `SoFCRendererBridge.cpp:776` where `objectKey` is hashed; if not, thread the
  object identity from the traversal context. Without it, the identity table needs a separate
  pass.
- **Main-thread marshalling latency.** Control ops queue onto the GUI thread; on a busy
  recompute the inspector must show pending/async state, never block. Mirror the existing
  queued pick/notification path.
- **Selection sync (client vs desktop `Gui::Selection`).** Selection is client-local today and
  intentionally so. Decide whether property ops implicitly sync the desktop selection (reuse
  the dormant `'P'`/`'B'` → `pickAndSelect` path) or stay stateless (address objects by name
  per request). *Stateless-by-name is favored* — it keeps the fast tier decoupled and matches
  the semantic protocol's stateless-operation shape; the dormant pick channel can drive
  desktop selection later if a co-editing story needs it.
- **Snapshot churn as feedback signal.** Animated scenes (water/fire) republish continuously,
  so "a snapshot arrived" is not proof an edit applied — rely on the `setProperty` ack, not on
  a frame update (a known trap from the debug handoff).
- **Property coverage.** `Placement`, `Link*`, `PropertyPythonObject`, expression-bound
  properties need editor semantics beyond a scalar field. v0 renders them read-only; stage
  richer editors. Expression bindings especially must be surfaced (read-only) before they can
  be edited, or the user edits a value the expression overwrites on recompute.
- **Framework longevity.** Solid is a compiler-level commitment with no React-compat escape
  hatch — accepted deliberately (§3). Keep the control-client and `fc:selection` contract
  framework-neutral so a swap, if ever needed, stays local to the components.
- **Where the DOM bundle is served.** Today `main.cpp`+`shell.html` are emscripten output. The
  new `web/` bundle needs its own build/serve lane alongside `build/wasm`; fold it into
  `wasm-viewer.sh`.
