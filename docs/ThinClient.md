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
bar, and a debug HUD (a panel of the DOM chrome once one is loaded — the viewer reports its
text as an `fc:hud` event and keeps its own `EM_JS` overlay box only for a page with no UI
layer, so a bare `fcviewer.html` still says what the renderer is doing). All "widgets" today (NaviCube, axis
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

**Pointer precision is part of picking, not a detail of it.** The pick radius edges and
vertices are resolved within is the desktop's `ViewParams::PickRadius` — CSS pixels sized
for a mouse on a ratio-1 screen — so the client scales it to the drawing buffer by the
device pixel ratio and widens it to 15 CSS px for a fingertip, which is millimetres of
contact aimed with no cursor. Preselection then follows the input's own affordances:

- **Mouse** — the cursor hovers; nothing to add.
- **Stylus** — a pen reports its position before it touches down, so `pointermove` with
  `pointerType === 'pen'` drives the same raycast, `pointerout` drops it. Free preselection,
  no gesture. (Emscripten's mouse callbacks never see it: a pen arrives as touch events.)
- **Finger — the touch loupe.** A single finger held still for 350ms preselects instead of
  orbiting; the highlight comes up under the finger, the drag *moves the pick* rather than
  the camera (lifted 20px clear of the contact point after the first movement so the
  fingertip stops covering its target), and the lift commits what is showing. A drag past
  the tap slop before the threshold, or a second finger, means the gesture was about the
  camera and cancels it. The threshold is a timer, not a frame check — a scene still
  streaming can be hundreds of ms per frame, and the hold must answer to the finger, not to
  the renderer.

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

// client → server  (the containers a pick cannot reach)
{ "id": 3, "op": "getProperties", "subject": "view3d" }     // the 3D view
{ "id": 4, "op": "getProperties", "subject": "document" }   // the document
{ "id": 5, "op": "setProperty", "target": "view3d",
  "name": "Render_AO", "value": true }

// server → client  (ack; the recompute + republished snapshot updates the view)
{ "id": 2, "ok": true, "recomputed": true }

// server → client  (unsolicited, structured — never a raw traceback)
{ "op":"error", "ref":2, "code":"ConstraintViolation",
  "message":"Length must be > 0" }
```

**Beyond v0.** `{"op":"cycles",...}` and `{"op":"cycles.camera",...}` are the
served viewport's ops -- the backend path traces this connection's view and
streams the frame; docs/CyclesIntegration.md sec 7.1 spells them.

**Subjects.** `getProperties` takes an optional `subject`: `object` (the default, and what
every v0 client asks for by saying nothing), `view3d` — the session's 3D view, where the
`Render_*`/`Shadow_*`/`RenderDebug_*` knobs live — or `document`. The two extra subjects
exist because **nothing in the scene stands for them**: they cannot be picked, so without an
addressing scheme of their own they are unreachable from a client that only knows how to
select geometry. Each descriptor carries the container it came from in `scope`
(`object` | `view` | `view3d` | `document`), and handing that value straight back as
`setProperty`'s `target` is how an edit reaches the same property — the client never has to
know the routing. A view property is the session's, not the model's, so it is assigned
outside the `AutoTransaction` and answers `recomputed: false`; it is not undo history and
there is nothing to recompute.

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
- **Menu + subject switcher.** A pick opens the card on the picked object; everything else
  hangs off one small round **menu button** (bottom-left, the corner the card, the pill, the
  NaviCube and the axis cross all leave free). It opens the card on the **3D view** — where
  the render knobs are and where they are worth turning, since you watch the result change as
  you drag the value — or on the **document**, and it carries the switches that are not
  properties of anything, starting with the **HUD**. It began as a single-action launcher and
  became a menu once there was a second thing to reach: a second unlabelled circle in the same
  corner competes for the same thumb. Actions close the menu; switches leave it open, so the
  state they just changed is visible. It stands aside only where a panel covers its corner —
  the narrow layout's bottom sheet. Inside
  the card a three-way segmented switcher moves between `Object` / `View` / `Document`
  without re-picking; `Object` is offered only while something is selected, since an empty
  card is a dead end. Switching resets the group/keyword navigation, because one subject's
  groups mean nothing to another. A new pick returns the card to the object — a pick is a
  statement about what the user is now interested in.
- **Draggable.** Both panels move: grab the card's header, or the pill anywhere but its
  buttons. Any fixed corner collides with something eventually — the card is tall enough to
  cover the NaviCube from either side, and what a panel hides is exactly what its own edits
  are changing — so the answer is that the panel moves, not that the anchor does. Pointer
  events (stylus/tablet behave like a mouse) with capture on the handle; the position is
  clamped to the viewport so a panel can never be dragged out of reach, re-clamped when the
  window shrinks, and remembered per panel in `localStorage`. Narrow screens keep the pinned
  bottom sheet.
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
  **anchored to the pick pixel** the client already has; radial layout for thumb reach. This
  is the *second* stage of the press: the first (350ms) is already the touch loupe below, so
  the menu opens over a target the user has been looking at, not one it is about to reveal.
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
  readout; previews before commit. The sketch tier is **input mirroring** (section 8): the
  browser streams its camera and pointer/key events, a per-client offscreen Coin viewer on
  the server replays them through the real sketcher, and the resulting scene changes come
  back as deltas. A client-side constraint solver, the earlier plan, is demoted to a
  prediction layer to add only if the measured round trip demands it.
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
  desktop selection later if a co-editing story needs it. **Revisited 2026-09-06:** the
  co-editing story is section 8, and the "desktop selection" it drives is no longer one
  object -- section 8.4 makes the selection a per-client instance behind the same accessor.
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

---

## 8. Input mirroring -- editing from the browser (2026-09-06)

The sketcher, and every other edit mode, is a large body of interaction code that lives in
view providers and their handlers: tool state machines, snapping, autoconstraints, pixel
tolerances for preselection, the cursor readout, on-view parameters. None of it was ever
going to be reimplemented in the browser, and a client-side constraint solver (the phase 4
plan above) would not have covered it anyway -- the solver is the small part. This section
records the design that replaces that plan: **stream the client's camera and input to the
server, replay it there against a per-client mirror of the client's viewer, and stream the
resulting scene changes back as the deltas the wire already carries.** VNC does the same
loop with pixels and is usable; this loop has fewer stages, and none of them touches a
framebuffer, so it should come in under VNC, not over it.

### 8.1 What the July experiment actually measured

An eager selection sync over the dormant `'B'` channel was tried in July 2026 and removed
because the highlight coming back from the server visibly lagged the instant local one.
The record of that session names the cause: the server's echo was a **full re-stream** that
sent the client through `setScene` and a warmup, stalling the view right after the local
highlight had already drawn. Three properties of the server at the time made that so, and
none is inherent to the idea:

- the connection loop ran on a **200 ms poll tick**, so any push waited for the next tick;
- every publish **re-sent the whole scene**; there was no delta;
- the echo **replaced** the client's state instead of merging into it.

Two of the three are gone since: publishes are content-addressed deltas
(`SceneStreaming.md`, 425 KB to 1.4 KB per publish), and the headless source coalesces on a
zero timer instead of a frame (`HeadlessServe.md`). The third is exactly stage 3 of
`SceneServerPort.md`: a push queued from the host still waits for the next tick because the
host-side senders append to the writer's queue without waking it. Stage 3 is therefore a
**prerequisite** of this section (landed the same day: `SceneServerPort.md` section 7.3,
every path at a fraction of a millisecond on loopback), and the first thing to do after it is to re-run the
July measurement on the dormant `'P'` channel: click, let the selection come back as a
delta, and log click-to-delta arrival. If that lands near the round-trip time plus a few
milliseconds, the rest of this section is building on a proven floor.

**Measured 2026-09-06, no browser in the loop** (a raw-socket client sends the `'P'` ray
and times the first binary frame back; `tests/gui/serve-selection-echo.py` is the
permanent form). Three boxes, alternating picks, loopback:

| serve shape | click to delta, median | p90 | of which pick dispatch to the GUI thread |
|---|---|---|---|
| `FC_BGFX_SERVE_SCENE` viewer under Xvfb (the July setup) | 6.5 ms | 8.3 ms | 1.3 ms |
| `Gui.serveDocument`, headless, no display | 2.9 ms | 3.2 ms | 1.4 ms |

A `'B'` batch of three picks comes back as one frame; a pick that changes nothing sends
nothing. The delta is 0.8 to 1.3 KB. So the diagnosis above holds: nothing in the loop
costs more than the pick's hop to the GUI thread plus the publish, and the rest of this
section builds on that floor. The headless row did not exist before the measurement: a
headless source selected and never published, because the selection root fed the render
cache only through its viewer and the source was not a selection observer at all
(`HeadlessServe.md`, fixed the same day).

**Windows, 2026-09-06** (`SceneServerPort.md` section 7.5, where the port was verified
there): the headless shape measures **3.9 and 4.3 ms** click-to-delta over the same
loopback, the batch's first frame at 3.8 ms -- the same shape of number as the Linux
row, a little slower. Reading it took a clock change: `time.monotonic()` is
`GetTickCount64()` on Windows through CPython 3.12, 15.6 ms of resolution, and reported
every echo as 0.0 ms; `tests/gui/serve-selection-echo.py` times with `perf_counter` on
both platforms now.

**macOS, 2026-09-07** (`SceneServerPort.md` section 7.5): **4.5 and 4.1 ms**
click-to-delta on the headless shape, the batch's first frame at 3.0 ms. Windows's
numbers to within a fraction of a millisecond, and the same shape as the Linux row
above. So the floor this section builds on is the same on all three platforms:
single-digit milliseconds, dominated by the pick's hop to the GUI thread and the
publish, with nothing platform-specific in between.

### 8.2 The shape

```
 browser                                      server (one process per session)
 -------                                      --------------------------------
 local tier: orbit, hover, pick  (unchanged)  per-client MirrorViewer
   |                                            camera + viewport = the client's
   | 'C' camera frames  (coalesced)  ---------> SoEventManager -> SoHandleEventAction
   | 'E' input  frames  (one-way)    ---------> ViewProvider::eventCallback
   |                                            -> sketcher mouseMove / pressed / key
   |                                            -> solver, edit graph updated
   v                                            -> change-driven traversal
 apply deltas  <--------------------------------  delta publish (editing feed, hilites)
 (prediction reconciled, never replaced)
```

Three rules make it fast, and each one is a lesson from 8.1:

1. **Navigation never leaves the client.** The camera goes up only so the mirror can pick
   with the client's exact projection; the client keeps rendering from its own camera, so
   orbit and zoom are as lag-free as today.
2. **The uplink is a one-way event stream.** No per-event reply, ever. X11 over a WAN is
   slow because of synchronous round trips, not bytes; this design must not grow one.
3. **No tick anywhere on the path.** Input is coalesced to one client frame, the server
   traverses on change, the writer is woken on push. A single 200 ms tick or debounce costs
   more than the entire budget below.

### 8.2a Ruling (2026-09-08): preselection stays local, selection round-trips

The split above left it open which of hover and click cross the wire. The ruling is that
they are not the same kind of event and must not get the same treatment:

- **Preselection never leaves the client.** Hover resolves and draws in the browser, as it
  already does -- the WASM viewer picks per face locally and tints on its own frame. No
  uplink frame, no mirror work, no echo, and nothing to reconcile.
- **Selection round-trips.** A click goes up as the `'P'` pick the wire already carries, the
  server makes the authoritative selection, and the result comes back as the delta that
  8.1 measured at 2.9 ms headless.

The reason is frequency against authority. Hover is the high-rate event -- a pointer moves
at the client's frame rate, so mirroring it means an uplink frame and a delta per frame,
and every one of them is in front of a highlight the client could have drawn itself in
microseconds. Selection is at human click rate and is the one that has to be *right*: it
feeds the tree view, the property panel, the task panels and the roughly 1500 call sites
behind `Gui::Selection()`, none of which the browser can reproduce. So the cheap frequent
thing stays local and the rare authoritative thing goes to the server, which is the
opposite assignment from VNC and the reason this loop can beat it.

This tightens rule 1 of 8.2: **navigation and preselection never leave the client.** It
also removes work from three places below -- 8.4's per-client instance is no longer needed
to keep two clients' hovers apart, 8.5's client-tagged hilite delta has no preselect to
carry in the first cut, and stage 3 of 8.9 is no longer a hover milestone.

**Where the ruling stops, and it is worth being honest about it.** In view mode a
preselect is only a highlight, so computing it locally costs nothing. Inside an edit mode
it is not: the sketcher's preselection is part of the tool state machine -- it decides what
a drag grabs, what snapping and autoconstraints offer, and what the cursor readout says --
and the browser has none of that logic. So in edit mode the pointer does ride the `'E'`
stream and the mirror does preselect, as 8.2 describes. The ruling is therefore about the
view-mode hover, which is where the traffic is; the edit-mode case is bounded because edit
mode is one object with small deltas, and confirming that bound is part of stage 4, not an
assumption to build on.


### 8.3 The mirror viewer: pure offscreen Coin, no widget, no GL

The claim to verify by building it: a client's viewer can be mirrored with Coin alone --
viewport, camera, event handling -- with no Qt widget and no GL context. Coin already
splits these: `SoRenderManager` holds the camera and the `SbViewportRegion` and touches GL
only inside `render()`, which the mirror never calls; `SoEventManager` runs
`SoHandleEventAction` over a scene graph with that camera and viewport, which is all a
pick, a preselect and a sketcher drag need. The serving process stays what `MultiDocServe.md`
section 7 prices it as: no GL library loaded, no GPU ever.

What stands in the way is the type. The edit path is written against
`View3DInventorViewer`, a `QuarterWidget`: `ViewProvider::mouseMove`, `mouseButtonPressed`,
`mouseWheelEvent` take one, and `ViewProvider::eventCallback` casts the event callback
node's user data straight to it. A count of what the sketcher, PartDesign, Part and the
core view providers actually ask of that pointer, from the sources on 2026-09-06:

| viewer method | calls | headless answer |
|---|---|---|
| `getSoRenderManager()` (camera, viewport region) | 18 | the mirror's `SoRenderManager` |
| `getSceneGraph()`, `getDocument()`, `getRenderCacheManager()` | 9 | shared with the document |
| `setupEditingRoot()`, `setEditing()`, `appendDetailPath()` | 7 | per mirror, as today |
| `getPointOnRay()`, `getPointOnViewport()`, `getCenterPointOnFocalPlane()`, `getViewportRegion()`, `getNearPlane()`, `getMaxDimension()`, `screenCoordsOfPath()` | 9 | camera math, view-less |
| `devicePixelRatio()`, `getPickRadius()` | 3 | values sent by the client |
| `getGLWidget()`, `getWidget()`, `screen()`, `setFocus()` | 13 | **widget: no answer** |
| `addGraphicsItem()`, `removeGraphicsItem()`, `redraw()`, `getGLPolygon()` | 5 | **widget or GL: no answer** |

So the extraction is: a `Gui::ViewerContext` (name to settle) base that carries the
view-less rows, `View3DInventorViewer` deriving from it unchanged in behavior, and the
view-provider entry points retyped to the base. That is a mechanical, wide, low-risk change
whose regression test is the whole desktop. The widget rows are the residue: the
`getWidget()` callers are almost all rubber-band and cursor code, `addGraphicsItem` is the
sketcher's on-view widgets. The mirror answers them with null and the DOM layer takes over
those surfaces (8.7); an edit mode that cannot run without a widget is discovered by
exactly that null, not by a crash in a headless process.

Smaller leaks of desktop state to fix on the way. The count was two when this was written
and is four as built (stage 1, 2026-09-08):

- `eventCallback` consults `QApplication::mouseButtons()` to decide whether Escape is safe,
  and the sketcher's click path reads the same global to cancel a rubber band. Both become
  `ViewerContext::mouseButtons()`, which the desktop answers from the application -- one
  pointer, so the same answer -- and a mirror answers from its client's button bits.
- The sketcher reads the pick radius from a global `ViewParams`. It becomes
  `getPickRadius()` on the context, which the desktop viewer already tracks from the same
  preference, so the desktop answer does not move.
- `eventCallback`'s Escape branch asks the **application's active view** whether it is
  selecting, and resets edit on the **active document**, rather than asking the view the
  event arrived through. On one window those are the same view; where they are not, the
  active one was never the right thing to ask.

A fifth is structural and is left to stage 4 rather than papered over here:
`DrawSketchHandler::getViewer()` fetches the viewer from `getMainWindow()->activeWindow()`,
so the sketcher's tool state machine reaches around its own view provider to the desktop's
active window. It compiles unchanged because it never sees the new type, which is exactly
why it needs naming: a mirror cannot serve a handler that asks the main window where it is.

**Closed at stage 4 (2026-09-09).** It asks the sketch's own view provider now, through
`ViewProvider::getEditViewer()`. Two things came with it. `DrawSketchHandler::activate`
read the active window's cursor and *purged the handler* if there was no window, so every
sketch tool would have refused to start in a served session -- a cursor is chrome, and not
having one is no reason to refuse to draw. And the recording of the edit viewer cannot live
in `ViewProvider::setEditViewer`: that hook is virtual, and `ViewProviderDragger`'s
override -- inherited by every geometry object, so by every sketch -- does not chain to its
base, which made the first version silently null for exactly the view providers that need
it. The view records it instead, in `ViewerContext::setEditingViewProvider`.

**Landed 2026-09-09 (stage 3 of 8.9).** `Gui::MirrorViewer` (`src/Gui/MirrorViewer.h`) is
that other implementation of `ViewerContext`: a camera, a viewport region, and the served
scene graph, built when a client first states a camera over the wire and dropped when its
connection closes. `SceneServeSource` keeps one per connection and resolves that
connection's picks through it. There is no Qt widget and no GL context anywhere in it, as
8.3 claimed there need not be.

The claim that paid for the stage is one this design had not made, and it is worth stating
plainly because it had been wrong in the serving process since the `'P'` pick existed:
**Coin gives a ray set with `SoRayPickAction::setRay` a radius of essentially zero and
ignores `setRadius()` entirely** (`computeWorldSpaceRay`, the `WS_RAY_SET` branch). So the
`setRadius(PickRadius)` the headless source called was a no-op, and a served click had to
hit geometry dead-on -- which for an edge or a vertex means it never hit at all. Resolved
through a mirror the ray is projected back to the viewport point the client made it from
and picked from there, so the pick radius applies as pixels of that client's canvas, and
the perspective cone widens with depth the way the desktop's does. Measured on a box:
with a five-pixel radius the edge holds out to four pixels clear of the silhouette and is
gone by six.

Four traps found in the doing. Every one of them is silent, and three of the four are
correct at the centre of the canvas -- which is the first place anyone checks:

- **The height angle does not mean the same thing on both sides.** Coin's default
  `ADJUST_CAMERA` viewport mapping applies the camera's height angle to the *smaller*
  viewport dimension -- below unit aspect it scales the view volume by `1/aspect` -- which
  is the Inventor convention a resizable window wants. A browser canvas applies it
  vertically at every aspect. The two agree on a landscape canvas and disagree on a phone
  held upright, which is most of what this tier is for. The mirror's camera therefore uses
  `LEAVE_ALONE` and the aspect ratio the client stated: the client's convention, taken as
  given. This is the one place where a mirror must deliberately not copy the desktop.
- **`SoCamera::getViewVolume(vp, resultvp, mm)` reads the aspect ratio off `resultvp`
  before assigning it from `vp`.** The out parameter has to go *in* carrying the viewport,
  which is what `SoCamera::getView` does and what nothing about the signature suggests.
  A default-constructed one hands it 100x100, and then every off-centre ray comes back
  projected through a square frustum -- a pick that lands somewhere else, and lands
  perfectly at the very centre where one would check it. Moot here once the mapping became
  `LEAVE_ALONE`, but it is a live trap for any other caller.
- **An `SbViewportRegion` carries two sizes, and setting one does not set the other.**
  `setViewportPixels` derives the region's *normalized* size from the pixel size against
  the *window* size, so stating only the pixels on a default-constructed region -- whose
  window is 100x100 -- leaves an 800x600 canvas describing itself as 8.0 x 6.0 of its
  window. Picking never noticed, because it reads the pixel size. Every row that answers
  where a pixel is in the world reads the normalized one, and those came back hundreds of
  times out.
- **The mirror does not aspect-correct a pixel, and the desktop does.** Nothing sets a
  desktop viewer's `SoCamera::aspectRatio`, so it stays 1 and `getViewVolume()` hands back
  a square frustum; `getNormalizedPosition` is what maps a pixel into that square. A
  mirror's camera states the client's real aspect -- that is what a mirror is -- so its
  frustum already carries it, and correcting again multiplies it in twice. Measured: every
  off-centre x landed exactly `aspect` times too far out. The mirror therefore has one
  convention rather than two, shared by the camera-math rows and the pick path.

The last two were found by asking a question the stage did not strictly have to ask: does
the view's camera math put a pixel where its pick path puts it? Nothing in stage 3 calls
those rows -- the edit modes that do arrive at stage 4 -- so both defects would have
shipped, and would have surfaced as a sketcher that snapped to the wrong place on a wide
canvas and to nowhere at all on a portrait one. That is the case for the unit oracle
having a row the feature does not yet use.

One deliberate omission: the mirror's `SoRenderManager` is never given a scene graph.
`setSceneGraph` refs the root and attaches a node sensor that fires on every change, and
one of those per connected client buys nothing -- this manager never renders, and
`getSceneGraph()` answers from the mirror's own member. What it is there for is the
eighteen `getSoRenderManager()` calls in the edit path, every one of which wants the camera
or the viewport region.

### 8.4 The selection stack: `SelectionSingleton` stops being single

`Gui::Selection` is process-global by construction: `SelectionSingleton::instance()`
returns one static object, reached through the `Gui::Selection()` accessor from roughly
1500 call sites in 197 files, including the preselect path inside `SoFCUnifiedSelection`
and everything the sketcher does with picks. `MultiDocServe.md` section 7 took that as a
fact and defined a room around it: one viewer's pick changes what every viewer sees. With a
mirror per client, two clients hovering would fight over one preselection, and a sketcher
drag in one browser would drive the other's highlights. (8.2a removes the first half of that:
hover no longer reaches the mirror. The second half stands -- an in-edit pick is still the
mirror's, and that is what the stack is for now.)

The simple start is to keep the accessor and make the instance **current** rather than
unique. `_pcSingleton` becomes the top of a stack of instances; a scoped guard pushes a
client's instance for the dynamic extent of one replayed event, and again for the publish
that follows it. Every one of the 1500 call sites then lands in the right instance without
being touched, because it runs inside that extent: the sketcher's preselect, the
`SoFCUnifiedSelection` handler, the selection-stack undo. What is per instance is what the
class already holds as members: the selection list, the current preselection, the
back/forward selection stacks, the notification queue, the selection style. What stays
shared is the document.

The one thing that does not fall out for free is the observer list. The tree view, the
task panels and the property view attach to the selection and expect one. In the first cut
they stay attached to the **room** instance -- the desktop's, or the headless source's
default -- and a mirror's instance notifies only its own client, through that client's
publish. The room's shared selection then becomes a **policy** rather than a fact: a click
in a mirror commits into the room selection (so every viewer and every panel sees it, as
today), while preselection and in-edit picks stay in the mirror's own instance. Later
options, per-client observers or a merged view for collaboration, are open, and this
layering does not close them.

**Landed 2026-09-09 (stage 2 of 8.9).** `Gui::Selection()` is unchanged as an
expression and now resolves to the current instance; `Gui::SelectionRoom()` is the new
accessor for the room, and `Gui::SelectionScope` is the guard that makes an instance
current for its own dynamic extent. Scopes nest and unwind innermost first; the room is
built on demand and is the only instance `destruct()` owns, an instance a scope pushed
belonging to whoever built it. Nothing on the desktop opens a scope, so there the
current instance is the room and selection behaves exactly as before.

Pinning the observers cost twenty lines and no design: `SelectionObserver::
attachSelection`/`detachSelection` name the room instead of the current instance, and so
do the eighteen `Attach`/`Detach` sites that use `Base::Subject` directly (nine task
panels and dialogs) plus the two places that reach a selection signal without an
observer at all -- the FEM post-object observer, and the tree view's refresh poke at the
property editor. Every one of them is room chrome, so every one of them was a
one-word change.

The one thing the extraction turned up that was not in the design: the constructor
subscribes to `App`'s `signalDeletedObject` and threw the connection away. That is
harmless for an instance that lives as long as the process and a use-after-free for a
mirror's, which dies with its connection, so the connection is now held as a
`scoped_connection`. `tests/src/Gui/SelectionStack.cpp` is the unit regression set --
accessor, nesting, per-instance state, observer pinning (including an observer built
inside a scope, which is the case that would otherwise go deaf when the scope closed),
and that deleted-object slot. `tests/gui/selection-room-panels.py` is the desktop
oracle: a real selection reaching a real tree view and property editor, which is what
stage 3 will re-read to say that a mirror's picks do **not** land there.

**The mirror's own instance landed 2026-09-09**, which is what stage 4 had left. A
`Gui::MirrorViewer` owns a `SelectionSingleton` and answers it from
`ViewerContext::selectionInstance()`; a desktop view answers null, meaning the room. The
guard is not opened by hand anywhere: **`Gui::ViewerScope` carries the selection with the
view**, so the two stacks can never be out of step -- an event handled in one client's
view and selecting in another's is the defect this shape exists to make impossible -- and
every existing call site inherits it. On the desktop no scope is ever opened, so nothing
about selection moves there.

**Which picks are the mirror's, as built.** An `'E'` event replayed through a mirror is
always handled in that mirror's selection: that covers the preselect the unified
selection root resolves, which 8.2a says is the client's business and not the room's, and
every pick an edit mode makes out of a replayed drag. A `'P'`/`'Q'` pick is the room's,
*unless* that mirror is the view the document's edit session is running in -- which is
"an in-edit pick is the mirror's own" written as a condition. So a click from a client
that is merely looking still commits into the room, as 8.2a rules, and a click from the
client that is editing does not reach the tree, the property panel, or any other viewer.

Entering and leaving an edit are the two transitions where that line had to be drawn
deliberately, and the answer is not the same on both sides of it:

- An edit mode clears the selection as it starts, as a convenience to a desktop user.
  That convenience belongs to the **room** -- what stops being highlighted is an object
  every viewer can see -- so `setEditOp` clears the room itself, and then enters the edit
  inside the scope. Left to the edit mode inside the scope it would have cleared an
  instance that was empty anyway, and left the sketch green in everybody's scene for the
  whole session.
- Leaving is inside the scope on both paths (the `resetEdit` op, and `~MirrorViewer` for
  a client that drops mid-edit), so the sketcher's parting "select the sketch I just
  left" is done to that client's instance. The mirror then **drops it**: it clears its own
  selection when the document says the session is over, which `Gui::Document::_resetEdit`
  signals after `finishEditing`. No client's selection outlives the session it belonged
  to.

**The exception the design had not seen: an edit mode's own observer.**
`ViewProviderSketch` is a `SelectionObserver`, and that observer is what colours the edit
geometry. Pinned to the room it would have heard nothing the browser picked, so the
picks would land correctly and nothing would turn green -- silently, since the desktop
never exercises it. So `SelectionObserver::attachSelectionToCurrent()` is the explicit
opt-out: it attaches to whichever instance is current at that moment, which is the room
on the desktop and the client's inside a served `setEdit`, and it *remembers* that
instance rather than following later scopes, because the session outlives any one
replayed event. `attachSelection()` is unchanged and still means the room, so every
other observer in the tree is untouched and the stage-2 rule stands where it was right.

**What a mirror's selection does not do is repaint the served graph.** The graph is one,
shared by every client, so a per-client highlight is not expressible in it: the room's
observer feeds it and the mirror's instance has none. That is the honest reading of "a
mirror's instance notifies only its own client" while the scene is shared -- in an edit
the client sees the edit mode's own geometry, which the sketcher colours itself through
the observer above, and a view-mode click still highlights through the room exactly as
it did at stage 3.

### 8.5 The wire

Both directions ride the existing `/scene` socket, so ordering is free and nothing new is
exposed to the tunnel or the gateway.

**Uplink, binary, one-way.** Two frame kinds beside the existing `'P'`/`'B'` picks:

- `'C'` camera: the `SoCamera` fields (type, position, orientation, height or height angle,
  near, far, aspect) plus viewport pixel size and device pixel ratio. Sent as fields, not as
  matrices, so the mirror's pick radius and the sketcher's pixel tolerances match the
  client's exactly. Coalesced: at most one per client frame, only when changed.

  **As built (stage 3).** Fifty-eight bytes: `'C'`, a type byte (0 orthographic, 1
  perspective), viewport width and height as little-endian `u16`, then thirteen
  little-endian floats -- position, orientation quaternion, height or height angle in
  radians, near, far, aspect, device pixel ratio, pick radius. Sizes and the pick radius
  are in *device* pixels, which is what the client renders at and what the desktop
  viewer's own pick radius is measured against. The frame is the trust boundary: a
  non-finite field, a zero canvas or an empty frustum is refused rather than adopted,
  because a NaN here would poison a mirror's view volume and every pick made through it.
  It is not an edit -- it says where one client is looking from, and nothing but that
  client's own mirror reads it -- so a view-only connection may send one, and does.

  The browser viewer packs it once per frame and compares the packed bytes, so an idle
  viewer sends nothing and a drag sends at most one frame per client frame. It is also
  sent immediately ahead of a pick, because the server resolves a ray against the camera
  it last heard about.
- `'E'` input: pointer move, press, release with button and modifier bits; wheel; key press
  and release with the Coin key code. Moves are coalesced so the latest position wins;
  presses and releases are never dropped and keep their order relative to the moves around
  them. Each frame carries the client's timestamp so the replayed `SoEvent` has a real
  time.

  **As built (stage 4).** Fifteen bytes: `'E'`, a kind byte (0 move, 1 press, 2 release,
  3 wheel, 4 key down, 5 key up), a modifiers byte (shift, ctrl, alt), a little-endian
  `u16` code -- the button number for the button kinds, the Coin key code for the key kinds
  -- three little-endian `i16` (x, y, wheel delta) and a little-endian `u32` client
  timestamp in milliseconds. A size distinct from the camera's fifty-eight and the pick's
  twenty-six, which is what tells the three apart.

  **Nothing in it is converted on the way up.** The position is the client's canvas pixels
  with the origin at the *top* left, exactly as a canvas reports it; the flip into Coin's
  bottom-up viewport happens in that client's mirror, against the canvas height the mirror
  already resolves its picks against, so a resize in flight cannot leave the event path and
  the pick path disagreeing about where a pixel is. A key event carries no position and
  keeps the pointer where the last move left it, because Coin's handlers read a position
  off every event.

  Two gates, and they differ from the camera's. An unrecognised kind is refused at the
  parser rather than passed on, because the mirror turns the kind into a Coin event type
  and an unknown one would become whichever event the default branch built -- a wrong event
  is worse than none. And the frame sits **behind** the view-only gate, where the camera
  sits in front of it: a camera says where somebody is looking, an input event moves
  geometry. Counted apart from the picks (`inputMsgs`/`inputWire`), because the rate of
  this channel is the one worth knowing on its own when 8.10b's question comes round again
  for edit mode.

  **Entering an edit** is not on this channel: it is the `edit` and `resetEdit` ops on the
  control channel (section 4.2), which carry the connection id so the session binds to that
  client's mirror. An `edit` from a connection that has stated no camera is refused --
  there is no mirror before the first `'C'` frame, and setEdit's own fallback would create
  a 3D view.

**Downlink: the deltas that exist.** Two demands on them that are new:

- The **editing overlay feed** (`OverlayEditing`, captured from `pcEditingRoot`) must delta
  at draw granularity during a drag -- the moved curves and their hilites, nothing else --
  and must never invalidate the main scene's ladder. Today that capture runs inside a GL
  render action on the desktop viewer; under the headless source it has to hang off the
  change-driven traversal instead. Whether the overlay feed already deltas that finely is
  the one open measurement in this section.
- Hilite deltas need a **client tag**: a mirror's preselection goes to its own client only;
  the room selection goes to everyone, as today. Under 8.2a the first cut has no preselect
  delta to tag at all -- view-mode hover never reaches the server -- so the tag is only
  wanted once edit-mode preselection starts riding the `'E'` stream.

**What the uplink does not yet carry (stage 3).** The `'P'` pick's flags byte says one
thing, "extend rather than replace", and the browser sets it for a sticky-multi or Shift
click. The client's own selection grammar is richer than that -- Shift promotes to the
whole object, the pick filter restricts what a pick may land on, and a plain click on an
already-selected sub-element cycles up to its object -- and none of that has a wire form,
so for those clicks the room selection ends up on the picked sub-element where the client
shows the whole object. The flags byte has seven spare bits and the grammar is the DOM
layer's anyway (section 5), so this belongs with stage 5 rather than with the mirror.

### 8.6 Reconciliation: prediction, then an idempotent echo

The local tier keeps doing what it does: hover and pick resolve client-side and draw
immediately. That is now a **prediction**. The server's answer for the same event arrives
as a delta that the client **merges**; if the prediction was right the delta is byte-
identical to what is already drawn and nothing on screen changes. This is the netcode
model, not the VNC model. VNC has no client state to reconcile; this design does, and the
July stall was precisely a reconciliation that replaced instead of merging. The rule is
therefore absolute: **a server echo may never trigger `setScene` or a warmup.** Inside an
edit mode there is no prediction at first -- the solver is on the server -- and the mirror's
deltas are simply applied.

**What the prediction costs, and it is not a rendering cost: the browser cannot witness its
own round trip.** As built, the client draws its highlight from its own raycast and
`applySnapshot` then *drops* the streamed selection feed and re-applies the local one; the
DOM layer's `fc:selection` event is fed from the same local list. So every surface a person
looking at the browser can see -- the highlight, the inspector panel -- shows what the
client decided, not what the server did. A round trip that resolved to the wrong element,
or to nothing at all, looks exactly like one that worked.

And it is worth being exact about how that fails, because the client is not merely failing
to show the server's answer -- it **receives that answer and overwrites it** with its own.
So the browser is not a weak signal to be weighed against others. It is a confident wrong
one, and the more carefully someone looks at it the more sure they will be.

Verify this loop by reading the **server's** selection --
`Gui::Selection().getSelectionEx()` in the serving process, which is what
`tests/gui/serve-mirror-pick.py` asserts -- because that is the only place the round trip's
answer survives; never the browser's display, which is the client's answer wearing the
server's clothes.

The concrete thing riding on that is the viewer's own `cameraQuaternion`, which turns its
eye/at/up frame into the orientation the `'C'` frame carries, and which has no test. Note
what does *not* cover it: a rotated camera round-trips in
`tests/src/Gui/MirrorViewer.cpp`, but that is the mirror's half, from a rotation the test
states rather than one the client computed. Marking the client's conversion by the picture
the client then draws from it is marking it by the thing it feeds. A transposed or
conjugated quaternion would leave the browser looking perfect while the server picked
somewhere else entirely.

### 8.7 The Qt-only chrome

Two surfaces of the sketcher are Qt widgets outside the scene and so outside the feed: the
task panel, which is already the DOM layer's job (section 4), and the on-view parameters,
the small entry widgets the sketcher places next to the cursor while a tool is active. The
latter need either a DOM counterpart driven by a small "widget" feed (position, label,
value, focus) or a rendered stand-in through the existing text ports. Keys must stream for
the same reason: Escape, Tab and numeric entry are how those tools are driven.

### 8.8 Budget

| hop | cost |
|---|---|
| input coalesced to one client frame | 0 to 16 ms |
| one way on the wire | RTT / 2 |
| mirror: pick on the edit graph plus one solver step | 1 to 5 ms |
| delta build plus writer wake | a few ms, event-driven |
| one way back plus one client frame | RTT / 2 plus 16 ms |

On a LAN that is under 40 ms end to end, dominated by the two frame boundaries; on a WAN
it is the round trip plus roughly 35 ms. VNC's loop adds a redraw, a framebuffer diff, an
encode and a decode, and caps the update rate at the encoder's. The comparison only holds
while rule 3 of 8.2 holds.

### 8.9 Staging

Each step is a standalone landing with the desktop as its regression oracle.

0. ~~**Stage 3 of the server port, then the measurement.** Wake the writer on push; re-enable
   the eager `'P'` send in a test build; log click-to-selection-delta. This decides
   whether 8.1's diagnosis was right before anything else is built.~~ Done 2026-09-06: the
   table in 8.1. The eager send was exercised from a socket client rather than a viewer
   build, which measures the whole server side of the loop and leaves the browser's own
   few hundred microseconds out; the viewer-side re-enable is part of stage 3 below.
1. **The viewer context.** Extract the view-less base from `View3DInventorViewer`, retype
   the view-provider entry points, kill the two desktop-state leaks in 8.3. No behavior
   change; the whole test set and a desktop sketch session are the check.
2. ~~**The selection stack.** Current-instance accessor, the scoped guard, observers pinned
   to the room instance. Again no behavior change on the desktop, where the stack has one
   entry.~~ Done 2026-09-09: the API and what pinning the observers actually cost are at
   the end of 8.4.
3. ~~**The mirror, selection only** (was "hover only", changed by 8.2a). A `MirrorViewer` per
   connection in the headless source and the `'C'` camera frame; a click arrives as the
   `'P'` pick the wire already carries, is resolved against the mirror's camera rather than
   the source's, and commits into the room selection. Hover is not in this stage and never
   becomes a wire event in view mode. This is the first user-visible latency number for the
   full loop, and 8.1's 2.9 ms is its floor.~~ Done 2026-09-09; what the mirror is and the
   two conventions it had to get right are at the end of 8.3, the frame as built is in 8.5.

   **The number: 7 to 10 ms**, click to scene push, over a real socket on loopback, against
   8.1's 2.9 ms floor -- so the mirror, the pick and the republish together cost a few
   milliseconds on top of what the server side already cost. That is the server half; a
   browser's own frame boundaries add the ~16 ms each way of 8.8's budget, and the wire adds
   the round trip.

   The pick commits through `Gui::Selection()` unchanged, which with no scope open *is* the
   room (8.4) -- so the policy of "a click commits into the room selection" is expressed by
   opening no scope, and the same line will land in the mirror's own instance the moment an
   in-edit pick opens one. The mirror therefore does not own a `SelectionSingleton` yet;
   stage 4 is where one is needed and where it should appear.

   Also re-enabled here, as 8.9 step 0 said it would be: the browser viewer's own send. It
   had been switched off because an eager sync made the backend re-pick and republish the
   whole scene, whose echo stalled right after the instant local highlight -- and the client
   still drops the streamed selection and re-applies its own on every snapshot, which is
   8.6's merge rule holding.

   Two oracles: `tests/src/Gui/MirrorViewer.cpp` (the ray-to-pixel round trip, the portrait
   case, the pick radius reaching an edge the ray misses) and `tests/gui/serve-mirror-pick.py`
   (the same thing over a real socket, with the latency number). The unit set is the one
   that would catch a regression here: the round trip is exact at the centre of the canvas
   whatever the view volume is, so a centre-only check proves nothing, and the corners are
   the test.
4. ~~**Edit mode.** `setEdit` under the mirror, the editing root captured by the change-driven
   traversal, keys streamed; a sketch drawn and dragged from a phone.~~ The server side is
   done 2026-09-09. The browser half -- a viewer that sends the `'E'` frame, and an
   interface to ask for an edit -- is what remains of this step.

   **The binding is the whole of it.** Everything the desktop reaches for when it starts an
   edit names the wrong thing in a serving process: `Gui::Document::setEdit` finds its view
   by asking `MainWindow::activeWindow()`, `setEditingTransform` and `getInEdit` go on
   asking it afterwards, and `DrawSketchHandler::getViewer()` -- the fifth leak 8.3 named
   and left -- asks it again from inside the tool state machine. With several browsers
   connected that question has no useful answer, and with none connected it has a worse
   one: **setEdit given no view does not refuse, it creates one**, so a served document
   entering edit would have opened a 3D window and a GL context in a process whose premise
   is having neither.

   So an edit session is bound to a `Gui::ViewerContext`. `Gui::ViewerScope` names the view
   whose input is being handled -- the same shape as `SelectionScope`, one replayed event,
   one dynamic extent, no call site retyped -- and setEdit asks it before it goes looking
   for a window. Nothing on the desktop opens a scope, so the desktop answer does not move.
   A sketch tool asks its own view provider (`ViewProvider::getEditViewer()`) instead of the
   window, and the two surfaces that genuinely need a Qt widget -- the cursor and the
   on-view parameters -- get a checked downcast that is null for a mirror. That null is what
   8.3 asked for: an edit mode that cannot run without a widget found by the null, in a
   place that can say so.

   **The editing root moved to `ViewerContext`**, where both implementations share it: the
   separator, the transform, and the rule that an edit mode's geometry is moved out of the
   view provider's root and put back. None of that was ever view work. What each view keeps
   is where the root hangs -- under the aux root on the desktop, and inside the *served*
   graph for a mirror, because the change-driven traversal is the only thing in a serving
   process that plays the part a redraw plays on the desktop, and it sees nothing that is
   not in that graph. In and out with the edit, so an idle client leaves no empty separator
   in everybody else's scene.

   **The `'E'` frame** (8.5) is the uplink an edit mode runs on, and the mirror grew an
   event root of its own -- this client's camera, this client's `SoEventCallback`, then the
   shared scene. Per client rather than in the shared graph, and that is the point: the
   served root is traversed once per client, so a callback node living in it would fire for
   every other client's events too.

   Three things that were not in the design and are worth carrying:

   - **`ViewProviderDragger::setEditViewer` does not chain to its base**, and every geometry
     object -- so every sketch -- inherits it. Recording the edit viewer inside that virtual
     hook therefore compiled, ran, and was null for exactly the view providers that need it.
     It is recorded by the *view* instead, in `ViewerContext::setEditingViewProvider`, which
     is one call site and cannot be bypassed.
   - **A mirror's `getPointOnRay` has to pick against the editing root**, not the view
     provider's, because `setupEditingRoot` has emptied the latter. This is the desktop's
     own rule, and it is what lets a drag grab what it drew.
   - **A connection can drop mid-edit**, and the mirror dies with it while `Gui::Document`
     still points at it. `~MirrorViewer` ends the whole session -- the whole session, not
     just this view's half, because a served document has no other view to carry it on in.

   Oracles: `tests/gui/serve-mirror-edit.py` end to end over a real socket (the `edit` op
   refused before a camera and accepted after, the sketch's graph moved and given back, an
   `'E'` event replayed and answered with a push, a client dropping mid-edit), and
   `tests/gui/sketch-edit-root.py` for the desktop, which reads the same rule through the
   view provider's child count and runs a drawing tool to watch the view's cursor change.
   The reading that discriminates the binding is that the served document has **zero** 3D
   views throughout: without the scope there would be one.

   **The mirror's own selection landed on top of this, 2026-09-09**, which is what the
   step had left over. A mirror owns a `SelectionSingleton`, `ViewerScope` carries it with
   the view, an `'E'` event and an in-edit pick select in it, and no client's selection
   outlives its session. The rule, and the two things it turned up -- the clear on
   entering an edit belonging to the room, and an edit mode's own observer having to
   follow the session rather than the room -- are at the end of 8.4. The probe grew the
   reading that discriminates it: the same click, sent in view mode and again while
   editing, reaching the room and then not reaching it.

   **The browser half landed 2026-09-09**, and with it the step is done: a real Chrome
   enters a sketch edit on a served document, drives it with pointer and keys, and leaves
   it. `tests/gui/serve-edit-browser.py` + `scripts/edit-drive.js` are the leg, and every
   reading but two is taken in the serving process; the two that are not are what the
   *viewer* believed, which is the half under test.

   What the viewer does, in `src/Gui/Renderer/wasm/main.cpp`:

   - **`window.fcviewerEdit(obj, mode, subname)` asks**, `fcviewerResetEdit()` leaves, and
     an `fc:edit` event says what the answer was. The viewer believes nothing until the
     answer arrives -- the state that routes its input follows the server, never the call.
     It also **forces a camera frame before the op**: under the default uplink policy
     (8.10b) a client that has not clicked has stated no camera, and the op is refused
     without one. That is the first thing the browser half had to get right, and it is
     invisible until you try it from a page nobody has clicked in.
   - **The pointer and the keyboard become `'E'` frames while a session is running**, and
     only then. The split is by button: the left one and the keys are the edit mode's, the
     middle and right ones and the wheel stay with the camera, which is this client's own
     question and one an edit mode has no opinion about. Moves are held and sent once a
     frame -- the same coalescing the camera does, for the same reason.
   - **The camera goes first whenever it is news**, before every `'E'` frame. 8.10b left
     this as "turn the per-frame policy back on for an edit", and the form the pick already
     uses turns out to be the better one: the mirror places the event with the camera it
     was last told about, so what an edit mode needs is not a camera sent often but a
     camera that is never behind the event it is placing. Same worst case, no staleness.

   And what the server does, which the client cannot do without: **it pushes both edges of
   the session to the connection whose view it is**. The leaving edge is the one that has
   to exist -- a session can end without the client asking (an Escape the sketcher handled
   itself, a host resetting it, the object deleted), and a browser still routing its left
   button into it would be talking to nothing. The client cannot infer it either: the scene
   delta that comes back from leaving looks like any other.

   **Three defects, and the first two were crashes that only a real browser reached.**

   - **`SoFCUnifiedSelection`'s pick path is desktop-only, and a replayed pointer move
     walks into it.** `getPickedList` dereferences its viewer four times -- the on-top
     path's root path, the late-pick paths, the graph the ray is applied to -- and the
     served root has no viewer. The preselect a move triggers goes straight there
     (`handleEvent` -> `onPreselectTimer`), so hovering a served document from a browser
     segfaulted the server. It answers "nothing picked" now, which is what `setHighlight`
     and `setSelection` beneath it already answered when they found no viewer: a client's
     click is resolved against its own mirror (8.3), the only place a per-client camera and
     pick radius exist, and a highlight in a graph every client shares is not expressible
     anyway. **The synthetic probe sent one move and never saw it** -- one move schedules
     the timer, and it takes a stream of them to reach the inline call.
   - **The sketcher asks for a Qt widget in three places**, and a browser drag across empty
     space reaches one of them: the rubber band took `getGLWidget()->height()` times the
     device pixel ratio, which is exactly the viewport's height in pixels wherever there is
     a widget and a null dereference where there is not. It reads the viewport now, and so
     does the constraint-icon projection -- whose pixels are compared against a Coin cursor
     position, so the widget's *logical* size was already the wrong unit on any hi-DPI
     desktop. The third, warping the pointer to a sketch point, returns instead: it moves
     the physical cursor of whoever is at the machine, and a sketch dragged from a browser
     must not reach across and do that.
   - **Escape leaves an edit through a deferred call**, which is the hazard 8.10 names and
     the first path known to walk into it: `ViewProvider::eventCallback` does not call
     `resetEdit`, it posts a zero-timer that calls it, and a timer runs with no scope open
     at all. The sketcher's parting selection would have landed in the room and left the
     sketch highlighted in every viewer's scene. `Gui::Document::resetEdit` opens the scope
     over the view itself now, for every caller -- which is why the control op no longer
     opens one of its own.

   A fourth, smaller: `Show`'s camera detail indexed `mdiViewsOfType(...)[0]` for the
   camera TempoVis restores, which is an IndexError in a process with no 3D view -- one
   traceback per served sketch session, and the restore lost with it.

   **The reading that discriminates the pointer stream** is that the server counts 68
   `'E'` frames for the 62 moves the page says it dispatched, and **zero before the session
   started**: in view mode the pointer never travels, which is 8.2a holding, and the same
   click that puts `Sketch.Edge1` in the room before the edit leaves the room empty during
   it.
5. **On-view parameters in the DOM** and whatever the widget residue of 8.3 turned up.

### 8.10 Open questions

- `Gui::Document::setEdit` admits one editing view provider per document, and a view
  provider is bound to a single edit viewer through `setEditViewer`. One editor per document
  is the first cut; two browsers in the same sketch is a collaboration question, deferred
  with the rest of collaboration.
- Whether the overlay feed deltas at draw granularity today (8.5), and what a per-move
  publish of a mid-size sketch costs.
- Pick radius and device pixel ratio: the client sends them, but a touch client wants a
  larger radius than a mouse, so the value is per client, not per document.
- The mirror has no frame; anything a view provider does "on the next redraw" needs the
  change-driven traversal to be that redraw.
- The client's selection grammar has no wire form (8.5): Shift-to-whole-object, the pick
  filter and the plain-click cycle all resolve to "extend or replace" on the way up. The
  flags byte has the room; the question is whether the grammar belongs on the pick or in
  the DOM layer's own selection op (section 5).
- The mirror answers `logicalDotsPerInchX()` with 96, the CSS reference, because it has no
  screen to ask and its client is a browser. Whether the edit modes that size things in
  millimetres want that or the client's real density is a stage 4 question.
- ~~Stage 4 left the mirror still committing its picks into the room~~ Done 2026-09-09;
  the instance, the rule for which picks are in-edit, and the one place the design had not
  seen -- an edit mode's own observer -- are at the end of 8.4.
- ~~**A scope is a dynamic extent, and an edit mode does not do everything inside one.**~~
  One path did do it, and it was found the day this was written: Escape does not call
  `resetEdit`, it posts a zero-timer that calls it (`ViewProvider::eventCallback`), and a
  timer runs with no scope open. `Gui::Document::resetEdit` opens the scope itself now, so
  leaving is covered however it is asked for. **The boundary is still real** -- anything
  else an edit mode defers, a queued call or a task panel answering later, still reaches
  the room -- and the lesson of the one instance is that the fix belongs in the operation
  rather than at the call site, because the call site is exactly what a deferred call has
  left behind.
- **A served document has no preselect, deliberately** (8.9 step 4): the shared root's
  `SoFCUnifiedSelection` answers "nothing picked" for want of a viewer. A per-client
  highlight in a graph every client shares is not expressible, and in view mode the client
  resolves hover locally anyway (8.2a). What it costs is the in-edit case: a click the edit
  mode itself does not handle selects nothing, where on a desktop it would have selected
  whatever was under it. Making that work means giving the pick path the view whose event
  is being replayed -- `ViewerContext::current()` is already the right answer -- but the
  four things it asks a viewer for (the root path, the late-pick paths, the hidden-line
  config, `appendDetailPath`) are genuinely `View3DInventorViewer`-shaped, so it is a stage
  of its own rather than a guard.
- `ViewProviderSketch` reads `QApplication::queryKeyboardModifiers()` when it turns a
  preselection into a selection -- the same class as the `mouseButtons()` leak below, and
  the same answer: a replayed event carries its own modifiers.
- `SoFCUnifiedSelection::handleEvent` reads `QApplication::mouseButtons()` twice, and the
  served root is shared by every client. It is reached only from a mirror's replayed
  events, so it is a leak of the same class as the four 8.3 closed rather than a live
  defect, and `ViewerContext::mouseButtons()` is the answer it wants. (Its selection half
  is no longer a leak: a replayed event selects in the mirror's instance.)
- `MirrorViewer::setSelectionEnabled` records a flag; the desktop's toggles `selectionRole`
  on its own selection root. A mirror's root is the shared served one, so the honest version
  of that toggle is per document rather than per client -- consistent with one editor per
  document, and worth stating rather than discovering.

### 8.10a The camera uplink is speculative work (experiment, next)

Stage 3 sends the `'C'` frame once per client frame whenever it changed, and again before
every pick. In view mode that is **entirely speculative**: `SceneServeSource::mirrorFor`
has exactly one reader, `pickAndSelect`, so the only moment a mirror's camera is read is a
click. An orbit therefore pushes a frame up the socket sixty times a second so that a
click which may never come can be answered -- and it does it on the uplink, competing with
nothing but costing on exactly the mobile links this tier exists for.

Three designs to measure against each other:

- **A, as built.** Per-frame coalesced send (pack, `memcmp`, send if changed) plus a forced
  send before each pick.
- **B, rate limited.** The same, throttled to a low rate while no selection is happening,
  still forced before a pick.
- **C, lazy and versioned.** No periodic send at all. The client keeps a camera **version**,
  bumped whenever the camera moves, and states the camera **with the click** only when that
  version differs from the last one it sent. Most of C is deleting the per-frame call: the
  forced send already there is C's send, and what is added is the version counter that lets
  it be skipped when nothing moved.

C has a variant worth measuring separately: carrying the camera *inside* the pick message
rather than as a `'C'` frame ahead of it. That is one WebSocket frame instead of two, saves
a header, and makes the pairing atomic rather than merely ordered.

What to measure: **uplink bytes per second through an orbit**, which is the case A is worst
at and the whole point; **click-to-scene-push**, against stage 3's 7 to 10 ms, since under C
the camera is on the click's critical path; and the edge-pick discrimination of
`tests/gui/serve-mirror-pick.py`, which is what says the lazy camera is still the right
camera. Count the bytes **on the server**, per connection -- the client counting its own
sends is the client marking its own work, and 8.6's rule applies to this as much as to
selection.

The counter-argument, to be priced rather than assumed away: stage 4 streams events, and a
sketcher tool computes its tolerances in the mirror on every move, so an edit session needs
the camera fresh continuously and C is a view-mode optimisation that stage 4 partly
reverses. Any later server-side view-dependent work -- prioritising the level ladder by
where a client is actually looking -- would want it too. So the question the experiment
should answer is not only what C saves, but what it costs to turn back on.

### 8.10b What it costs, measured (2026-09-09)

The experiment of 8.10a, run. Four policies against one served document, over a real
socket: an idle camera, then ten seconds of a camera in continuous motion, clicked through
every 1.5 s. The bytes are counted **on the server**, per connection --
`SceneClientInfo`'s uplink counters, read through `Gui.serveClients()` -- and the bench
cross-checks its own count against the server's, which is what says the counters count the
right thing. `tests/gui/camera-uplink-bench.py`; wire bytes, so the RFC 6455 header a
client masks each frame under is included.

| policy | idle | through the motion | camera frames/s | click-to-push, median / worst |
|---|---|---|---|---|
| `frame` (A) | 0 B/s | **3888 B/s** | 60.5 | 7.5 / 9.1 ms |
| `rate` (B, 10 Hz) | 0 B/s | 614 B/s | 9.3 | 8.1 / 8.8 ms |
| `lazy` (C) | 0 B/s | 57 B/s | 0.6 | 5.3 / 8.3 ms |
| `lazy1` (C', camera inside the pick) | 0 B/s | **55 B/s** | 0 | 7.6 / 8.9 ms |

Through the ten seconds of motion, in messages and bytes: A sent 612 messages (606 camera,
6 picks) and 38 976 bytes; B 99 and 6144; C 12 and 576; C' **6 messages and 546 bytes** --
one per click, nothing else at all. **A to C' is 71x fewer bytes and 100x fewer messages.**

Four things the numbers say that the argument in 8.10a did not.

**A is expensive while moving, not always.** Every policy costs zero through the idle
phase, A included: it coalesces on the packed bytes, so a still camera sends nothing. The
waste is real but it is bounded by how much the user orbits, which is a smaller fraction of
a session than "sixty times a second" suggests.

**C does not cost latency.** That was the concern that made the experiment worth running --
C puts the camera on the click's critical path -- and it is not visible: every policy lands
in the same 5 to 8 ms median, inside stage 3's 7-10 ms. The camera and the pick are queued
to the GUI thread in order and the pick's own work dwarfs a 58-byte parse.

**The message count matters as much as the byte count, and scales worse.** 612 messages a
second per client is 612 wakeups a second on the connection's read path, and that is per
viewer: ten viewers orbiting is six thousand. The bytes are 39 kB/s and survivable; the
wakeups are what a serving box would feel first.

**C in its two-message form has a transport hazard C' does not.** The first bench run
measured C at **50.6 ms** a click against everyone else's 7.5 -- because C writes a camera
and then a pick, two small segments back to back, on a socket that has been silent since
the last click. Nagle holds the second until the first is acknowledged and the peer delays
that acknowledgement: a textbook ~40 ms of nothing. Setting `TCP_NODELAY` on the bench
client, one variable, took it to 7.8 ms. The server already sets `tcp::no_delay` on
accepted sockets and a browser sets it on its own, so this is not a defect of the wire --
but C' never has two frames to hold, which makes it the form that is right on a client
whose transport we do not control.

**The browser leg.** The bench is a synthetic client implementing the same policy over the
same frames, and it is not `src/Gui/Renderer/wasm/main.cpp`. Section 8.6's rule cuts both
ways: a browser cannot be trusted to judge its own selection, and a bench cannot be trusted
to stand in for the browser's code. So `tests/gui/camera-uplink-browser.py` serves the
document and the built viewer, drives Chrome through an orbit and four clicks under each
`?camup=`, and reads the same server-side counters. What it found, per policy, through a
ten-second orbit:

`CAMUP_REAL=1` puts it on the real GPU -- a headful window on the WSLg desktop, WebGL2
through ANGLE over D3D12, at the compositor's 60 Hz. That is the tier the numbers come
from; headless swiftshader can only show the shape. Through a ten-second orbit there, with
the page confirmed drawing at **60.0 rAF/s and 601 dispatched moves**:

| policy | camera frames through the orbit | orbit bytes/s | clicks | camera frames with the clicks | selection after |
|---|---|---|---|---|---|
| `frame` | 596 (56.8/s) | **3633 B/s** | 4 | 4 | `Box.Face4` |
| `rate` | 93 (8.9/s) | 567 B/s | 4 | 4 | `Box.Face4` |
| `lazy` | **0** | **0 B/s** | 4 | **1** | `Box.Face4` |
| `lazy1` | **0** | **0 B/s** | 4 | **0** | `Box.Face4` |

**The real browser reproduces the bench**: 3633 B/s against the bench's 3888, and 567
against 614. So the synthetic client was not standing in for the browser's code after all --
the two agree, which is the strongest form the answer could take. Every policy picks, and
picks the same thing. `lazy` states its camera once across four clicks -- the coalescing
working, the three clicks from an unmoved camera saying nothing -- and `lazy1` sends no
camera frame at all, ever, because it rides inside the pick.

Two things to know about running it. The page's frame rate is the measurement, so the
harness reports the gesture's own `moves` and `rAF/s`: too few camera frames could be the
policy coalescing, a slow page, or a gesture that never reached the camera, and those are
three different things. And **an earlier version of this table read 0.6 frames a second and
was written up as a limit of the tier** -- it was not, it was a defect of the fix in 8.10c
below, caught only because 0.6 fps on a GPU that probes at 61 is not a believable number.

Getting the table at all took finding and fixing something much larger, below.

**The default is now `lazy1`.** The measurement says C' by a wide margin, the correctness
witness holds in both legs, and nothing on the server reads a mirror's camera except the
click that carries it. `?camup=frame` remains, and it is one enum value away for the edit
mode that will want it back.

**What it costs to turn back on**, which 8.10a asked to price rather than assume away: one
enum value. The per-frame send is not deleted, it is a policy; stage 4 turns it back on for
edit mode, where a sketcher tool recomputes its tolerances in the mirror on every move and
the camera must be continuously fresh. The policy is per client and per moment, so entering
edit mode is where it flips, and nothing about C' makes that harder.

**The condition attached to it.** `SceneServeSource::mirrorFor` has exactly one reader
today, and that is the whole justification. A second reader that wants to know where a
client is looking *now* -- prioritising the level ladder by view is the obvious one -- makes
a lazy camera wrong, and wrong quietly: the camera it reads will not look stale, it will
look plausible. That is written at `mirrorFor` itself, where someone adding such a reader
will be standing.

### 8.10c The browser tier could not draw at all, and nothing said so

Found by the browser leg above, and much the larger of the two findings.

The WASM viewer defaults to four-sample MSAA. Its scene colour target is RGBA16F while
colour management is on, and **a multisampled RGBA16F colour buffer is not available on
every WebGL2 backend**. Where it is not, every `createTexture` for the scene targets
returned an invalid handle, `createFrameBuffer` could not be built from invalid
attachments, and the frame path did what it is supposed to do with a missing scene
framebuffer: bail to the host and let Coin draw. In a browser there is no Coin. The viewer
drew nothing, retried the whole target build on the next frame, and bailed again -- for
ever, at a cost heavy enough that the page stopped answering synthetic input, which is how
it was found rather than by looking at it.

Two things made it invisible. The message is a bgfx error line, and no one reads a browser
console unless something else already went wrong; and the fallback it takes is a real,
correct fallback on the desktop, so nothing about the code path reads as broken. The
handoff had recorded that nobody had built `build/wasm` in a while, and stage 3 built it
without ever running it in a browser. This is what was waiting.

The obvious fix is the check the OIT targets a few hundred lines below already make of
their own formats -- ask `BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA` of the scene colour
format and of D24S8, and drop to no MSAA when the backend says no. **It does not work.**
Written and measured: WebGL2 answers that bit for RGBA16F and then fails every create, so
the capability is a claim and the create is the fact. The check passed and the viewer stayed
black.

So the fallback is empirical instead. When the scene framebuffer fails to build and more
than one sample was asked for, that is latched for the process, said once, and the view
rebuilt immediately without multisampling -- `init(false)` and not `init(true)`, because an
MSAA change re-decides `m_oit` and so which program set exists at all. Verified end to end:
`4x MSAA scene targets could not be created on this backend -- rebuilding without
multisampling`, then `view init 1100x900 msaa 1`, then `scene consumed: 6 draws, 2 meshes`.

**And the default was wrong, which is the actual repair.** The desktop turned MSAA off when
lines and points started resolving their own coverage analytically -- the measurement is in
`View3DInventorViewer::getNumSamples`, and the same comment in `BGFXViewLifecycle.cpp` says
"MSAA is off by default". The browser viewer had simply never been brought in line: it still
asked for four samples. So the default asked a backend for something it does not have, and
the fallback above is now what catches an explicit `?msaa=4` rather than what every browser
runs into. Both defaults are off now, the viewer's and the standalone lib's.

**The fallback had a second-order bug, and it is the more instructive half.** With it in
place the viewer drew -- correctly -- and ran at **0.6 frames a second** instead of 60. The
frame path decides whether to rebuild a view with

```
progChanged = _BGFXLib.standaloneSamples != view->msaaSamples || ...
```

which compares what was ASKED for against what the targets were BUILT with. The fallback
lowered the second and not the first, so every frame saw a program change, rebuilt every
target the view owns, and did it again. The picture was right the whole time, which is why
it read as "this tier is just slow" and got written into this document as a property of
swiftshader. Both sides go through one `_BGFXLib::effectiveSamples()` now, so the build and
the did-it-change test cannot disagree.

Four things worth keeping. **`?msaa=0` is the first thing to try when the browser viewer
shows nothing.** A bail-to-host path is only a fallback where there is a host to bail to. A
capability bit is a promise, not a result: where the cost of believing it is the whole
scene, try the thing and react to what happens. And **a fallback that changes what was
built must change what the rebuild test compares against, or it re-fires for ever** -- the
symptom is a correct picture at a fraction of the frame rate, which is far easier to
rationalise than a wrong one.
