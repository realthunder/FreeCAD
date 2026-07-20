# UI framework research for the bgfx viewer (WASM + mobile + slim desktop)

Research date: 2026-07-20. Claims verified against primary sources (repos, official
docs, license pages); unverifiable items flagged inline. Context: C++20/CMake, bgfx
renderer (GL now, WebGL2 via Emscripten in browser — bgfx's WebGPU backend is
**native-Dawn-only as of Jan 2026**, so browser = WebGL2 for the foreseeable future,
per [bkaradzic's 2026-01-12 post](https://bkaradzic.github.io/posts/webgpu/)), LGPL
distribution.

---

## How the industry actually does this (baseline finding)

Every serious web CAD/design product uses **native-speed core in a WASM/WebGL canvas
+ browser DOM for all UI chrome**:

- **Figma**: C++/Emscripten canvas engine (WebGL → WebGPU with WebGL fallback since
  Sept 2025), everything else React DOM. Only document-locked adornments (selection
  handles, cursors) are in-canvas.
  ([2015 blog](https://www.figma.com/blog/building-a-professional-design-tool-on-the-web/),
  [WASM blog](https://www.figma.com/blog/webassembly-cut-figmas-load-time-by-3x/),
  [WebGPU 2025](https://www.figma.com/blog/figma-rendering-powered-by-webgpu/))
- **AutoCAD Web**: 30-year C++ core via Emscripten ("Fabric"), JS/DOM shell around it
  ([QCon 2018 talk](https://www.infoq.com/presentations/autocad-webassembly/);
  "React specifically" is widely reported but not pinned to an Autodesk primary source).
- **Onshape**: server-side geometry kernel, browser WebGL tessellation rendering +
  DOM UI (Angular inferred from their public example apps, not a primary statement).
- **Unity web games**: standard pattern is a DOM/React overlay with
  `pointer-events: none` + selective re-enable
  ([react-unity-webgl](https://react-unity-webgl.dev/)).

**Nobody renders inspector panels inside the GL canvas.** The reasons are the exact
weak spots of in-canvas UI: mobile soft keyboard, IME/CJK, accessibility, scrolling
physics, clipboard, file dialogs — all free and correct in DOM, all chronic
reimplementation projects in-canvas.

---

## Candidate-by-candidate findings

### 1. Dear ImGui — MIT, v1.92.8 (2026-05-12), very active

- **bgfx integration: first-party quality, in the bgfx tree.**
  [`examples/common/imgui/`](https://github.com/bkaradzic/bgfx/tree/master/examples/common/imgui)
  implements font-atlas texture, transient VB/IB, per-command scissor, image shaders;
  bgfx vendors ImGui at **1.92.8 WIP** (essentially head). Every bgfx example
  exercises this path daily. It's example-tier code — you'd vendor/own a thin copy
  (~300 lines) and implement the new v1.92 `ImGuiBackendFlags_RendererHasTextures`
  dynamic-texture protocol.
- **Fonts/CJK**: v1.92 (June 2025) dynamic font system — on-demand glyph
  rasterization, **no more pre-baked CJK ranges**, runtime font scaling
  ([FONTS.md](https://github.com/ocornut/imgui/blob/master/docs/FONTS.md),
  [#8465](https://github.com/ocornut/imgui/issues/8465)). This removed ImGui's
  historic CJK pain.
- **Docking**: still a separate branch, maintained in lockstep (`v1.92.8-docking`
  tag exists); "experimental" indefinitely but massive production install base.
- **WASM**: official Emscripten examples +
  [live web demo](https://www.dearimgui.com/webdemo/); measured payload **~1.8 MB
  uncompressed wasm** (official demo, measured 2026-07-20); sokol+ImGui apps
  ~1.1 MB. Marginal cost for our viewer: roughly a megabyte.
- **Touch/mobile — the weak spot**: no multi-touch
  ([#3711](https://github.com/ocornut/imgui/issues/3711)), no kinetic scrolling
  (DIY via synthetic wheel), no wasm IME/soft-keyboard (hidden-`<input>` hack
  required, with known focus/backspace bugs
  [#5805](https://github.com/ocornut/imgui/issues/5805),
  [#5133](https://github.com/ocornut/imgui/issues/5133)), **no accessibility at all**
  ([#4122](https://github.com/ocornut/imgui/issues/4122)).
- **CAD suitability**: Tables API = property grids/tree-in-column; docking = panels;
  themable but never native-looking. Adopters: Tracy, ImHex, RemedyBG, hundreds of
  engine tools.

### 2. RmlUi — MIT, v6.2 (2026-01-11), very active (commits through 2026-07-18)

- **The "real UI system" among the permissive options.** HTML/CSS-like documents,
  CSS2 + selected CSS3: **flexbox yes, tables yes, grid no**; animations,
  transforms, decorators, media queries, MVVM **data bindings** (the intended path
  for trees/property grids — no built-in tree or datagrid widget, that part is DIY).
- **bgfx backend: you write it.** Required
  [`RenderInterface`](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html)
  surface is small and bgfx-shaped: compiled geometry (VB/IB), textures, scissor;
  optional: transforms, stencil clip-masks, layers/filters (box-shadow/blur — the
  heavy part). Existing community backends are stale:
  [martonp96/rmlui-bgfx](https://github.com/martonp96/rmlui-bgfx) (last push
  2023-11, pre-dates the 6.0 render-interface rewrite, reference only). One source
  claims **RavEngine maintains a bgfx RenderInterface** (via
  [discussion #186](https://github.com/mikke89/RmlUi/discussions/186)) — treat as
  *possible but unconfirmed*; check RavEngine's repo directly.
- **WASM**: Emscripten is an officially supported compile target (SDL/GL3 backends,
  since 5.0). No published wasm size figures (unverified; must measure).
- **Touch**: **native touch handling + inertial scrolling landed in 6.2**
  (Jan 2026) — a major recent improvement. No pinch/rotate gesture recognizers (DIY).
- **Text**: FreeType default (replaceable font engine), **HarfBuzz shaping plugin +
  RTL since 6.0**, IME support on desktop backends since 6.0. CJK works.
- C++17 minimum, clean CMake, vcpkg. Adopters: The Thing: Remastered, ROSE Online,
  Unvanquished, TruckersMP.

### 3. LVGL — MIT, v9.5.0 (2026-02-18), very active (~24k stars)

- **bgfx composition: the best-documented texture path of any candidate.**
  `lv_opengles_texture_create()` / `..._create_from_texture_id()` renders the whole
  LVGL display **into a GL texture you supply**
  ([docs](https://docs.lvgl.io/master/integration/embedded_linux/opengl.html)) —
  bgfx composites it as an overlay quad. v9.5 also added a full **OpenGL draw unit
  via NanoVG**. Fallback: plain memory-buffer flush → bgfx texture upload. No bgfx
  backend needed, just glue.
- **WASM: proven** — official
  [lv_web_emscripten](https://github.com/lvgl/lv_web_emscripten) port, live demos at
  lvgl.io/demos; measured payload **~1.1 MB wasm** (demo page, 2026-07-20).
- **Touch-first: the best in class here.** Built-in kinetic + elastic scrolling,
  pinch/swipe gesture recognizers with multi-touch, on-screen keyboard widget, even
  a Pinyin IME widget. Bidi + Arabic shaping built in; FreeType/TinyTTF.
- **Costs**: pure C API (verbose from C++20, no RAII), no tree widget,
  embedded-grade visual polish, essentially zero desktop/mobile-app adopters (all
  embedded HMI).

### 4. Slint — v1.17.1 (2026-07-07), active; **blocked for this project today**

- C++ API is first-class (CMake + `.slint`→C++ codegen) but the core is Rust — Rust
  toolchain required for every target. Mature touch/gestures (pinch/rotate handlers
  in 1.16), Android-from-C++ landed in 1.17; iOS still Rust-only.
- **Disqualifier: "Only Rust supports using Slint with WebAssembly"**
  ([official docs](https://docs.slint.dev/latest/docs/slint/guide/platforms/web/)),
  maintainer-confirmed Jan 2026
  ([discussion #10425](https://github.com/slint-ui/slint/discussions/10425)) —
  **C++ + Emscripten is unsupported**. Also heavy: measured **~8.3–10.7 MB
  uncompressed wasm** (~4.3 MB brotli) for official demos.
- bgfx composition is inverted from what we want (Slint owns the window; bgfx as GL
  underlay/texture). Licensing: GPLv3 / Royalty-Free 2.0 / commercial; RF permits
  desktop+mobile+web apps but **forbids embedded use and forbids exposing Slint
  APIs from your app** — friction for an extensible LGPL platform. Monitor, don't
  adopt.

### 5. NoesisGUI — v3.2.13 (~Apr 2026); **legally unshippable in LGPL FreeCAD**

Technically strong (full WPF/XAML control set, official WebGL/WASM, clean
`RenderDevice` abstraction that fits a bgfx frame; BG3/Hytale-grade adopters; indie
tier €195/project, Pro €9,000+/platform + mandatory support). But the
[EULA](https://www.noesisengine.com/legal/eula.php) **prohibits reverse
engineering** — directly conflicting with LGPL-2.1 §6's requirement that the
combined work permit reverse engineering for debugging — and forbids SDK
redistribution, so downstream builders/forks can't build. Both existing bgfx
integrations are dead (2021/2022). **Ruled out on license, not tech.**

### 6. Ultralight / Coherent Gameface / Sciter — all ruled out

- **Ultralight 1.4** (Apr 2025; promised 1.4.1 never shipped as of Jul 2026):
  proprietary binary-only below Enterprise; free tier "<$100K indie, PC only,
  limited features"; Pro $3,000/yr/app. **No WASM target.** Incompatible with
  distro/conda-forge redistribution norms.
- **Coherent Gameface**: per-title enterprise contracts, no public pricing, no WASM.
  AAA-proven (Minecraft, Civ 7) but implausible for open source.
- **Sciter**: alive (GitLab activity 2026-07-18), free binary-only use, engine not
  open source, **no WASM build** — Sciter itself tells users to run their HTML in a
  real browser for the web tier.

### 7. The small libraries

| Lib | Status | Verdict |
|---|---|---|
| **NanoVG-bgfx** | **In the bgfx tree** ([examples/common/nanovg](https://github.com/bkaradzic/bgfx/tree/master/examples/common/nanovg)), zlib | Not a toolkit — the vector *drawing layer* for crisp custom chrome/HUD/overlays through bgfx. Use it. |
| **vg-renderer** ([jdryg](https://github.com/jdryg/vg-renderer)) | Alive (pushed 2026-05-14), BSD-2 | Faster NanoVG-style renderer native to bgfx; substrate only. |
| **Clay** (v0.14, 2025-06-06) | Active, zlib | Flexbox layout engine only (~15 KB wasm); pairs with a NanoVG/bgfx draw layer; brings no widgets. |
| **Nuklear** (4.13.3, 2026-05-05) | Active, MIT/PD | Strictly dominated by ImGui (old-style font atlas, no docking, weaker tables). |
| **microui** (v2.02, 2024-08) | Dormant/"finished" | Too small for a CAD UI. |
| **NanoGUI** (mitsuba fork, pushed 2026-06) | Alive, wasm-capable | No renderer seam — bgfx port = rewrite; small widget set. Skip. |
| **Turbo Badger / CEGUI / FastUIDraw** | Dead / trickle (CEGUI last release 2016) | Skip. |
| **egui via C++** | **No usable C++ binding exists** ([discussion #1360](https://github.com/emilk/egui/discussions/1360)) | Not an option without adopting Rust. |
| **Elements (cycfi)** | Alive but Skia/Cairo-bound, no wasm target | Not a fit. |

### Cross-cutting: canvas text input in the browser is structurally bad

Regardless of framework: mobile browsers only raise the soft keyboard for real DOM
editables — every canvas UI uses the hidden-`<input>` hack, with documented
focus/IME/candidate-window bugs. The future fix,
[EditContext API](https://developer.mozilla.org/en-US/docs/Web/API/EditContext_API)
(Chrome/Edge 121+, Jan 2024), is Chromium-only and has no Emscripten binding.
**This is the strongest single argument for DOM-overlay UI in the browser tier.**

---

## Ranked recommendations

### (a) Fastest path to a usable touch-capable viewer UI, WASM + desktop, one C++ codebase through bgfx

**1. Dear ImGui (docking) on a vendored bgfx backend, + NanoVG-bgfx for viewport
chrome.** Already living in the bgfx tree at our exact ImGui version, MIT, ~1 MB
wasm, wasm-proven, tables/docking cover panels/trees/property grids, v1.92 dynamic
fonts solve CJK display. A working UI in days, identical in `fcviewer.html` and a
slim desktop shell. Budget explicit work items: touch-to-mouse synthesis + DIY
kinetic scroll (Emscripten `html5.h` touch callbacks; floooh's multitouch pattern),
and the hidden-input hack for text fields in wasm.
**Biggest risk:** touch/IME/accessibility are *permanently* DIY — ImGui upstream has
no multi-touch, no soft-keyboard/IME story, and no a11y, and none is on its roadmap.
Fine for a viewer; a ceiling for a product.

**2. LVGL 9.5 composited via `lv_opengles_texture_*`** — if touch-first matters more
than desktop polish. Best built-in touch UX of any candidate (kinetic/elastic
scroll, pinch, OSK, Pinyin IME), MIT, proven ~1.1 MB wasm, documented
render-to-GL-texture path that drops straight into a bgfx overlay quad.
**Biggest risk:** the C API and embedded heritage — building and maintaining a
CAD-grade tree/property UI in C-style LVGL is slow, and no notable desktop app has
done it.

### (b) Best long-term fit for a polished CAD product UI across browser/mobile/desktop

**1. Hybrid: DOM/React overlay over the fcviewer canvas in the browser (+ optionally
Tauri/webview shell for touch desktop), with ImGui-on-bgfx retained as the
in-canvas/engineer UI everywhere.** This is the Figma/AutoCAD-Web/Onshape
architecture — the only pattern with proven CAD-product polish on the web. Our
existing WebSocket/pick protocol is already the right bridge shape; in-process it
becomes embind/`cwrap` calls. DOM gives IME/CJK, mobile keyboards, accessibility,
scrolling physics, clipboard, and file handling for free; only 3D-locked adornments
(handles, snap markers, dimensions) stay in the bgfx canvas. Tauri 2.0 (MIT/Apache,
stable 2024-10, Android/iOS) can reuse the same web UI as a slim desktop/mobile
shell.
**Biggest risk:** the UI codebase forks — the browser tier is TypeScript/DOM while
native tiers need something else (or a webview), and state synchronization across
the bridge is a permanent tax; Linux WebKitGTK is the weak leg of the webview-shell
story.

**2. RmlUi 6.2 with a new bgfx `RenderInterface` — the single-codebase
alternative.** The only permissive (MIT), alive, C++-native framework offering real
styling (CSS-like + flexbox + data bindings), HarfBuzz shaping, desktop IME, native
touch + inertial scrolling (new in 6.2), and official Emscripten support — while
staying renderer-agnostic so it genuinely draws *through* bgfx on every platform.
The required backend surface (compiled geometry/textures/scissor) is modest; stencil
clip-masks and layer/filter effects are the optional heavy part. Escalation path
from recommendation (a): the ImGui bgfx plumbing built first (transient buffers,
sequential view, scissor) is 80% reusable.
**Biggest risk:** we own the bgfx backend forever (no maintained one exists — verify
the RavEngine claim), and tree-view/property-grid widgets must be built from data
bindings; plus in-canvas text input in the browser still inherits the
hidden-input/IME hack.

**3. (Watchlist) Slint** — best-in-class declarative C++ tooling and mobile
trajectory, but hard-blocked today by no-C++-WASM, an inverted composition model,
~4 MB+ compressed wasm, and license conditions (no API re-exposure) that chafe an
extensible LGPL platform. Re-evaluate if C++/Emscripten support ships.

### Suggested concrete sequencing

1. Now: vendor bgfx's ImGui backend (upgrade to the 1.92 `RendererHasTextures`
   protocol), docking branch, NanoVG-bgfx for viewport overlays → touch-capable
   viewer UI on desktop + WASM.
2. In parallel, keep `fcviewer.html` growing a thin DOM layer for exactly the things
   canvas UI does badly: text fields (IME/keyboard), file open, menus — the hybrid
   pattern from day one.
3. Decide RmlUi-vs-DOM-first once the viewer UI's scope is clear; the bgfx UI
   plumbing from step 1 transfers either way.

**Flagged unverified items:** bgfx-vendored ImGui docking-vs-master status;
RavEngine's RmlUi-bgfx backend; RmlUi wasm payload size; LVGL/Slint official wasm
size figures beyond the measured demos; Onshape's internal framework; AutoCAD Web's
use of React specifically.
