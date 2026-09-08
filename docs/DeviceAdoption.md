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
2. **Startup ordering.** An RHI warm-up surface beside `GLSurfaceWarmup`, and a
   `warmup()`/`prepare()` path that can take a QRhi device instead of a
   QOpenGLWidget. Independent of the viewport, safe to land early.
3. **The Coin on-screen audit. Cost measured (below); the pixel question is
   still open, and that is what gates step 4.**
4. **The QRhiWidget viewport.** Blocked on step 3.
5. **Device adoption.** Last, because it is the part already proven by probe.

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
