# Device adoption: Qt owns the device, bgfx adopts it

**Status (2026-09-08): design agreed, not started.** This is the macOS-side
implementation plan for the route the two renderer sessions settled on. The
shared cross-platform design is `docs/RenderEngine.md` section 7.10, written on
the Linux box; this file is the part that only matters here, plus the staging.

## 1. Why this route

The bgfx renderer draws a frame; something has to put it on screen next to
everything Qt draws. Three routes were weighed.

- **Aliasing / GL interop.** bgfx renders into a texture the Qt GL context can
  sample. Viable on Linux and Windows. **Dead on macOS**: Apple's compatibility
  profile caps at GL 2.1, `prepare()` refuses anything below 3.1
  (`BGFXRendererP.h`, the version test above the platform branch), and Apple
  does not share resources across profiles. The 2.1 cap is structural, not a
  driver limit: nothing in `src/Gui` requests a GL version or profile at all --
  the one `setProfile(QSurfaceFormat::CoreProfile)` in the tree is commented out
  at `Quarter/QuarterWidget.cpp:163` -- because Coin's fixed-function drawing
  needs the compatibility profile, and macOS caps that at 2.1.
- **Native child surface.** bgfx presents to its own window, embedded with
  `QWidget::createWindowContainer`. **Disqualified on every platform.** A native
  child surface composites ABOVE the parent's painting on Cocoa, X11, Wayland
  and Win32 alike, so every non-native Qt widget over the viewport goes dark --
  `Gui/Flag.cpp:364-368` (Flag widgets under a FlagLayout on the viewer) and the
  whole transparent overlay dock system in `Gui/OverlayWidgets.cpp`. Measured
  here: a QLabel childed to the container stays non-native (`internalWinId` 0),
  i.e. painted into the backing store, which Cocoa composites UNDER the subview.
- **Readback.** GPU -> CPU -> `glTexSubImage2D` -> textured quad. Works
  everywhere including GL 2.1, preserves every Qt overlay, needs no bgfx change.
  Correctness path, not the destination -- see the numbers in section 2.
- **Device adoption (this route).** Qt owns the graphics device through QRhi;
  bgfx adopts the same device; bgfx's render target is imported as a
  `QRhiTexture`. Same device, so no export/import and no external memory. A
  `QRhiWidget` composites into the widget tree the way `QOpenGLWidget` does, so
  Flags and the overlay docks survive.

## 2. What readback costs, measured

Two boxes, full serialization on both (no pipelining), 1920x1080 unless noted.

| step                                    | this box (Iris Pro 6200) | RTX 2000 Ada |
|-----------------------------------------|--------------------------|--------------|
| GPU->CPU readback                       | 1.71 ms (Metal)          | 2.51 ms (Vulkan) |
| `glTexSubImage2D` RGBA8/UNSIGNED_BYTE   | 0.94 ms                  | 0.57 ms      |
| `glTexSubImage2D` BGRA/UNSIGNED_INT_REV | 2.68 ms                  | 1.97 ms      |
| textured quad                           | 0.31 ms                  | ~0.3 ms      |
| **total, fast upload**                  | **2.96 ms (18% of 60Hz)**| **3.4 ms (20%)** |

At 1440p the discrete box is ~5.2 ms (31%) and at 4K ~10.7 ms (64%), which is
what makes readback a staging step rather than a destination.

**RGBA beats BGRA on both vendors and both APIs** -- 2.9x here, 3.5x there. The
BGRA path falls off the fast DMA route rather than merely being slower. So read
into an RGBA8 target instead of swizzling. Corroboration: Qt's own
`QRhiWidget::grabFramebuffer` on this box returns `QImage::Format_RGBA8888`.

Note the integrated GPU here is only ~1.5x faster than the discrete one at 1080p
despite having no PCIe transfer at all. The serialization dominates, not the bus.

## 3. What macOS needs, and what already works

Verified against the vendored bgfx and Qt 6.11.1 on this box.

- **bgfx adopts an external device on Metal.** `renderer_mtl.cpp:874`:
  `m_device = (MTL::Device*)g_platformData.context;`, falling back to
  `CreateSystemDefaultDevice()` only when null, then `retain`s it. Structurally
  identical to `renderer_vk.cpp:2006-2011`.
- **bgfx Metal has an explicit headless path.** `renderer_mtl.cpp:1127`:
  `nwh == NULL` traces "Headless." and skips swapchain creation. So this route
  needs no window handle at all.
- **Qt exports what bgfx wants.** `QRhiMetalNativeHandles` (`rhi/qrhi_platform.h:169`)
  carries `MTLDevice *dev` and `MTLCommandQueue *cmdQueue`;
  `QRhiTexture::nativeTexture()` and `createFrom(NativeTexture)` bridge both
  directions; `QRhiWidget` exposes `rhi()`, `colorTexture()` and `render(cb)`.
- **It comes up here.** A `QRhiWidget` with `Api::Metal` initialises on this box
  (Intel Iris Pro 6200, macOS 12), reports backend "Metal", hands out a live
  device and queue, renders a frame, and -- the point -- a child QLabel stays
  non-native and composites above it.

### The price: a Qt private-API dependency

`QRhiWidget` itself is a public QtWidgets header. `QRhi`, `QRhiTexture` and
`QRhiMetalNativeHandles` are not: they live under the versioned private path
(`include/qt6/QtGui/6.11.1/QtGui/rhi/`) and need `Qt6::GuiPrivate` to link. The
public `QRhiWidget` returns those types from `rhi()` and `colorTexture()`, so
there is no way to use the widget for this route without taking the private
dependency. `qrhi.h` says so in its own words:

    // This file is part of the RHI API, with limited compatibility guarantees.
    // Usage of this API may make your code source and binary incompatible with
    // future versions of Qt.

**This codebase currently links no Qt private module at all** -- Route D would be
the first. `Qt6GuiPrivate` is present in the conda env here, so it builds; the
cost is version fragility, in a tree that deliberately tracks upstream's Qt
closely and has already moved 6.11.1 -> 6.11.2 between boxes. It applies to
every platform, not just this one: the Vulkan and D3D12 forms of this route need
the same headers for their own native-handle structs.

That is a decision to take deliberately rather than discover at link time.

**Pinning Qt in the dev env does not settle it.** `.conda/freecad/conda-meta/pinned`
already carries `qt6-main ==6.11.1`, which fixes THIS box. What governs a
released package is the run-export the Qt package declares, and qt6-main's is

    "weak": ["qt6-main >=6.11.1,<7.0a0"]

-- effectively unpinned across all of Qt 6. So a build made against 6.11.1
installs and runs against 6.12, 6.13, anything short of Qt 7. For public Qt API
that is correct by design and is why the range is so wide. For RHI it is exactly
the case Qt refuses to guarantee, and the failure is not a link error at install
time but a struct layout that moved under code already compiled.

Making it actually safe means the feedstock carrying an explicit tight run
constraint (`qt6-main ==6.11.1`) for our package rather than inheriting the
run-export. That works, and it costs: the package becomes uninstallable beside
anything wanting a different Qt patch release, and every conda-forge Qt
migration forces a coordinated rebuild before users can update. That is a
distribution decision, not a renderer one.

The mitigation is real and cheaper than it sounds. `qrhiwidget.h` includes only
`qwidget.h` and forward-declares `QRhi`, `QRhiTexture` and the rest, so
subclassing QRhiWidget needs no private include at all -- only the code that
CALLS into QRhi does. The surface that route needs is about six symbols:
`QRhi::nativeHandles()`, the `dev`/`cmdQueue` fields of the Metal handle struct,
`QRhiTexture::nativeTexture()` and `createFrom()`, and
`QRhiCommandBuffer::beginPass`/`endPass`. Confining those to one translation
unit does not remove the dependency; it bounds what a Qt release can break and
makes the blast radius reviewable.

**Not yet measured:** how much `qrhi.h` actually churns between Qt releases.
Only 6.11.1 is on this box. That is checkable -- the other box has 6.11.2, and
Qt's history is public -- and it is the number that would turn this from a
stated risk into a sized one.

### The queue is backend-specific

`PlatformData` has a `queue` field (`bgfx.h:644`) but its comment says D3D12 and
the code agrees: used at `renderer_d3d12.cpp:1305`, referenced zero times in the
Vulkan and Metal backends.

| backend | device        | queue                                          | sync needed |
|---------|---------------|------------------------------------------------|-------------|
| D3D12   | adopted       | adopted (`platformData.queue`)                 | one queue by design |
| Vulkan  | adopted       | `vkGetDeviceQueue(family, 0)` -- may BE Qt's   | host lock if same, semaphores if not; compare handles at runtime |
| Metal   | adopted       | `newCommandQueue()` at `renderer_mtl.cpp:4740`, unconditional | always two queues: MTLEvent or completion handler |

So the sync abstraction must cover same-queue-lock and cross-queue-signal, and
the backend picks. Metal can never take the lock path.

## 4. The two constraints

1. **Coin gates the viewport.** A `QRhiWidget` viewport has no GL context, so
   Coin cannot traverse anywhere -- there is nothing to composite over the frame
   with. Note this is NOT a matter of inverting the composite: on screen the
   direction is already backend-first (`renderScene()` asks the backend, Coin
   traverses over it; `docs/CoinRetirement.md` 88-110 records
   `renderToFramebuffer()` being changed to match). Everything Coin still draws
   must be gone or moved to an overlay feed BEFORE the viewport can change.
   That makes this the Coin-retirement endgame, not a self-contained change.
2. **`bgfx::init` is once per process**, so the QRhi device must exist before the
   FIRST 3D view -- otherwise bgfx creates its own device and every later view is
   stuck with it. Works in a prototype, fails on the second document.

   **The hook for this already exists.** `Application.cpp:3007` brings the
   backend up under the splasher when render cache is 3, before any 3D view,
   using a hidden 1x1 `QOpenGLWidget` named `GLSurfaceWarmup` that MainWindow
   keeps for the window's lifetime (`MainWindow.cpp:464-469`), and seeds
   `setMaxViewIds` there because view ids are a startup option. Route D needs an
   RHI analogue of that widget and a `warmup()` that takes it. The ordering
   requirement is therefore already satisfied architecturally; what changes is
   what the warm-up hands over.

## 5. Staging

1. **Design agreed.** This file plus `RenderEngine.md` 7.10. Done.
2. **Startup ordering. DONE** -- section 9.
3. **The Coin on-screen audit. DONE** -- sections 7 and 8. Cost is fixed and
   small, per-frame geometry emission is zero, with the scope limits both
   sections state.
4. **The QRhiWidget viewport.** Unblocked by step 3 for the scene class it
   measured. Not started.
5. **Device adoption.** Last, because it is the part already proven by probe.

Beside the staging, and not on it: **the readback composite is implemented**
(section 10). It is the route this one is weighed against, it is what makes a
Metal or Vulkan session put a frame on the screen at all, and until stage 4 it
is what those sessions use.

## 6. Dead code this retires

The non-GL branch of `prepare()` creates a `QWindow` and hands `winId()` to bgfx
as `platformData.nwh`. On macOS that never worked and cannot: a Qt6 `QNSView`'s
backing layer is a `QContainerLayer`, so bgfx takes its `setLayer:` branch -- and
`QNSView` overrides `-setLayer:` and ignores it (verified against `NSView` with
`class_getInstanceMethod`, in both orderings). bgfx is left holding a
`CAMetalLayer` detached from the view hierarchy: it renders, it presents, nobody
sees it, and no warning fires because the layer pointer is non-null.

So the comment at `BGFXRendererP.h:1765` -- which attributes Metal showing
nothing to `BGFXView::blit` standing aside -- is incomplete. That is true, and
there is a second independent cause. Under this route bgfx needs no `nwh` at
all, so the honest change is deleting the surface path rather than repairing it.

## 7. The Coin audit, part one: what the residual traversal costs

Run here 2026-09-08, `FC_BGFX_METAL=1` with render cache 3 and
`RenderDebug_Timing` on, 1400x900 viewport, camera rotating every tick so no
frame is a no-op, ~900 frames per run. The instrument is the tree's own:
`Render::FrameOutside::Coin` brackets `inherited::actualRedraw()` at
`View3DInventorViewer.cpp:6591`, and `FC_RENDERER_PARALLEL_GL=1` forces the
internal pass back on, which is the control -- it is the one switch that makes
`SoFCRenderer::render()` NOT return early on `canSkipInternal()`
(`SoFCRenderer.cpp:2866-2873`).

| Part::Box solids | coin, skip active | coin, internal pass forced on |
|------------------|-------------------|-------------------------------|
| 192              | 0.54 ms/frame     | 2.24 ms/frame                 |
| 768              | 0.56 ms/frame     | 5.74 ms/frame                 |

Two things follow, and the second is the one worth having.

- **The skip is real and effective.** 4.1x at 192 solids, 10.2x at 768.
- **The residual is FIXED, not proportional to the graph.** Four times the
  geometry moves it by 0.02 ms. The forced-on column over the same step goes
  2.24 -> 5.74, which is the positive control: the instrument does see
  scene-dependent cost when there is any, so the flat column is a measurement
  and not a broken probe.

So the thesis in the comment above that bracket -- "if it is a large share of the
frame it is walking the whole scene graph for no pixels -- a bug, not a cost" --
does **not** hold here. At cache mode 3 the traversal is not walking the scene
for pixels. That is the good outcome, and it means step 4 is not blocked on a
performance bug.

**What this does NOT establish, and why step 4 is still gated.** A fixed 0.55 ms
is not zero, and cost is not the question Route D asks. Route D asks whether the
residual traversal *draws*, because a QRhiWidget viewport has no GL context for
it to draw into -- a traversal that costs nothing but emits one GL call is still
fatal, and a traversal that costs 0.55 ms doing no drawing is harmless. Nothing
here separates those. The remaining work is to count GL emission inside that
bracket, not milliseconds.

Scope of the run, so it is not read as broader than it is: one scene type
(Part boxes), no workbench-specific scene graph, no selection highlight, no
section planes, shadows or hidden-line, dpr 1. The `chrome` bucket (0.27-0.40
ms) is separate from `coin` and is known GL work outside the traversal -- axis
cross, NaviCube, dimensions -- which the nine overlay feeds exist to absorb.

Incidental, and a separate defect: two of the four runs segfaulted at exit,
after the data was collected and the frames were done. Not chased.

## 8. The Coin audit, part two: does the traversal EMIT?

Section 7 measured what the residual traversal costs and said plainly that cost
is the wrong question -- a QRhiWidget viewport has no GL context, so what gates
step 4 is whether anything still *emits* GL. This is that measurement.

**Method.** A `DYLD_INSERT_LIBRARIES` shim interposing the GL drawing entry
points (`glDrawArrays`, `glDrawElements`, `glBegin`, `glCallList(s)`,
`glDrawPixels`, `glBitmap`, `glRectf`, `glClear`) and counting them. This works
here for a reason peculiar to this box: on a Metal session the backend draws
through Metal, so **every GL call left in the process comes from Coin or Qt**,
not from the renderer. That separation does not exist on a GL backend, which is
why this number is easier to get here than on the Linux box.

Note SIP strips `DYLD_*` across `/bin/bash`, so the variable has to be re-added
with `env` after the conda wrapper, not exported before it.

Same harness as section 7: ~870 frames per run, camera rotating, 1400x900.

| config              | objects | drawarrays | drawelements | glBegin | drawpixels | bitmap | clear |
|---------------------|---------|------------|--------------|---------|------------|--------|-------|
| skip active         | 0       | 2674       | 0            | 0       | 0          | 0      | 1779  |
| skip active         | 192     | 2824       | 576          | 0       | 0          | 0      | 1870  |
| skip active         | 768     | 2792       | 2304         | 0       | 0          | 0      | 1838  |
| `PARALLEL_GL` (ctl) | 192     | 2732       | 525572       | 38456   | 2622       | 2622   | 2683  |

**Per-frame geometry emission with the skip active is ZERO.** The
`glDrawElements` totals are 576 at 192 objects and 2304 at 768 -- exactly 3 per
object, and *independent of the 870 frames*. They are a one-time cost at scene
construction, not per-frame drawing. Nothing else geometry-shaped fires at all:
`glBegin`, `glCallList`, `glDrawPixels` and `glBitmap` are flat zero.

**The control proves the counter sees emission when there is any.** Forcing the
internal pass back on takes `glDrawElements` to 611/frame and `glBegin` to
45/frame, and lights up the `drawpixels`/`bitmap` paths that are zero otherwise.

**What is left per frame is small, flat, and probably not Coin.** About 3.2
`glDrawArrays` and 2.1 `glClear` per frame, and the telling detail is that
`glDrawArrays` is ~3.2/frame in *every* configuration -- including the control
where Coin draws all 192 solids, and including the run with an empty document.
A count invariant to both scene content and whether Coin is drawing is not
Coin's scene drawing. `QOpenGLWidget` compositing its FBO into the window is the
obvious candidate, and that work does not survive into a QRhiWidget viewport at
all: Qt does the same job through QRhi instead.

**Verdict for step 4: the gate is open enough to proceed, with one caveat.** The
fear was that a QRhiWidget viewport would silently lose drawing Coin still does.
It does not do any, per frame, at least for this scene class. What has NOT been
established is attribution of the residual ~3.2 draws: "invariant across every
configuration" is strong evidence they are Qt's compositing, but it is inference,
not a bracket-scoped count. Confirming that needs counters scoped to the
`FrameOutside::Coin` span rather than process-wide totals.

Scope carries over from section 7 unchanged -- one scene class, no selection
highlight, section planes, shadows or hidden-line, no workbench-specific graph.
Those are exactly the cases that would introduce chrome the nine feeds must
absorb, and they remain unmeasured.

## 9. Stage 2: the startup ordering, as built

Constraint 2 of section 4 says the device must exist before the first 3D view,
because `bgfx::init` is once per process. Section 4 also says the hook for that
already exists and the change is not WHEN the device comes up but WHOSE it is.
That is exactly what landed, and it is smaller than the design made it sound.

**The seam.** `src/Gui/Renderer/DeviceAdopt.h` declares `Render::AdoptedDevice`
-- api, device, queue, and for Vulkan the instance, physical device and queue
family -- and four functions in `Render::QtRhi`. The header includes no Qt RHI
header at all, and nothing else in the tree does either.

**The one translation unit.** `QtRhiDevice.cpp` is the only file that includes
`<rhi/qrhi.h>` and `<rhi/qrhi_platform.h>`. It holds a `QRhiWidget` subclass
(`WarmupSurface`), the four functions, and the switch that turns a live `QRhi`
into an `AdoptedDevice`. Its Qt-private surface is what section 3 predicted:
`QRhi::nativeHandles()`, `QRhi::backend()`, `QRhi::create()`, the `dev` /
`cmdQueue` / `physDev` / `gfxQueue` fields, and `backendName()`. CMake scopes
`FC_RENDERER_QT_RHI` to that file alone with `set_source_files_properties`, so
the switch keeps saying where the private headers are allowed; without
`Qt6::GuiPrivate` the file still compiles as a set of functions that answer
"no".

**The surface.** `MainWindow`'s constructor creates a hidden 1x1
`RhiSurfaceWarmup` beside `GLSurfaceWarmup`, under `FC_RENDER_RHI=1`. Beside,
not instead: adoption settles whose device bgfx runs on, and the Qt GL context
is still what the frame is composited through until stage 4.

**The warm-up.** `Application.cpp` runs the adoption first and the existing
widget warm-up straight after. `RendererLib::warmup(const AdoptedDevice &, ...)`
is a new overload beside the QOpenGLWidget one; `BGFXRendererLib` implements it
by checking that the backend the session asked for and the API Qt's device
actually is are the same thing -- they are configured independently, so that is
a real mismatch to catch -- and then calling `BGFXRendererLibP::prepareAdopted`.

**`prepareAdopted` is `prepare()`'s device half and nothing else.** It sets
`platformData.context` to the adopted device, sets `platformData.queue` only on
D3D12 (section 3's table), passes NO window handle, and asks for a 0x0
resolution. It deliberately does not build the Qt GL context or the offscreen
surface: there is no widget yet to take a pixel format from. The widget warm-up
that follows finds `currentType != Noop`, skips `bgfx::init`, and does exactly
its remaining half -- the GL context, the anchor view, the shader programs.
That split is what lets Route D change the device without touching startup
ordering that was already right.

Two things worth stating because they are easy to get wrong from the docs
alone:

- **`m_headless` is not what the Metal backend's "Headless." trace means.**
  bgfx's `Context::init` sets `m_headless` only when the WHOLE `PlatformData`
  is null, and refuses a non-zero resolution in that case. Setting
  `platformData.context` clears `m_headless`, so the resolution rule does not
  apply -- while `renderer_mtl.cpp` still takes its no-swapchain path, because
  that one is keyed on `nwh` alone. Both are wanted, and they are different
  tests.
- **The anchor view still matters.** `removeView()` of the last view calls
  `shutdown()`, so a session whose only warm-up was the adopted one would tear
  the device down when the first 3D view closed. Running the widget warm-up
  after the adoption keeps the anchor view that has held the device since it
  was introduced.

**What stage 2 does NOT establish.** Whether Qt's own `QRhi` is up at the
moment the warm-up runs. A `QRhiWidget` draws through the top-level's backing
store and the warm-up is deliberately early, under the splash screen. When
Qt's is not up, `warmupDevice()` falls back to a `QRhi` it creates itself and
says so in the log, because that satisfies the ORDERING constraint (bgfx has a
device, and not one it chose) without satisfying stage 4 (a QRhiWidget viewport
must share the WINDOW's device to hand its texture over without a copy). The
fallback covers Metal and D3D only: `QRhiVulkanInitParams` wants a
`QVulkanInstance`, and one made here would be a second instance beside Qt's,
which is the opposite of what adopting a device is for.

## 10. The readback composite, as built

Section 2 costs this route from a standalone probe. It is now in the frame.

`BGFXView::blitReadback` (`BGFXViewLifecycle.cpp`) is the composite that
`BGFXView::blit` could never be: `blit` wraps bgfx's own attachments in a GL
framebuffer, which requires the attachment to BE a GL texture, which is true
only while bgfx runs on GL. The readback route copies the finished frame to
system memory, uploads it into a GL texture and draws a quad, and it does that
on any backend.

- **The copy is queued where the capture is queued**, before the frame
  boundary, riding on `ViewCapture` -- the last view id -- so what it copies is
  the finished image, present pass included.
- **RGBA8 / `GL_UNSIGNED_BYTE`, never BGRA.** Section 2 measured RGBA 2.9x
  faster here and 3.5x on the discrete box; BGRA falls off the fast DMA route
  rather than merely costing a swizzle, and bgfx hands back RGBA anyway.
- **The flip is in the texture coordinates, not in the buffer.** `originBottomLeft`
  says whether the backend's rows came back top-down; inverting `v` costs
  nothing, and flipping the CPU image would be a full copy per frame.
- **The quad is fixed-function.** The destination is the QOpenGLWidget's
  framebuffer, and that context is a compatibility one in every build of this
  application -- Coin needs it, which on macOS is also what caps it at GL 2.1.
  GL 1.1 is the one thing guaranteed to be there. `glPushAttrib` carries the
  whole state block back for Coin, which traverses next.
- **Pipelined by default.** A frame that arrives before its copy has landed
  redraws the previous image rather than waiting, so the screen trails the scene
  by a frame or two. That lag is as much this route's cost as the milliseconds
  are, which is why the report counts stale frames beside the times.
  `FC_BGFX_READBACK_SYNC=1` spins frames until the copy lands, which is the
  fully serialized form section 2 costed.
- **Colour only.** `blit` also transfers depth; this does not. At render cache 3
  Coin emits no per-frame geometry (section 8), so nothing is currently depth
  testing against the frame -- but that is a measured fact about one scene
  class, not a guarantee, and it is the first thing to check if chrome ever
  looks wrong over a readback composite.

**Controls.** `FC_BGFX_READBACK`: 0 off, 1 (default) wherever the GL blit
cannot run, 2 always -- including on GL, which is the only way to compare the
two routes on one box, one scene and one camera. Per-step costs are drained
into a `render readback composite (ms/frame)` line beside the existing frame
lines, under `RenderDebug_Timing`.

**Measuring it.** `scripts/composite_cost_probe.py` is one leg -- a fixed grid
of `Part::Box` solids, a camera that turns every tick so no frame is a no-op, a
fixed duration. `scripts/composite-cost.sh` drives the legs and reduces the
report lines, because the route is read from the environment once at startup
and a leg therefore has to be a process.

**Consequence for the backend list.** The stated reason Metal and Vulkan are
opt-in was that they render and capture but cannot reach the screen. That hole
is now closed and the gates stay anyway, with the reason rewritten in the code:
what remains is a cost, and a default nobody has run and looked at is a worse
answer than an opt-in.
