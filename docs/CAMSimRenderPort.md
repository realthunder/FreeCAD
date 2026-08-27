# CAM simulator render port

Porting the CAM simulator's rendering off raw OpenGL. **Stage 1 is
COMPLETE (2026-08-25)**: all eight steps landed; the simulator draws
entirely through the facade, and the raw-GL path is deleted. Section 7
records what the execution added to that plan. **Stage 2 -- the
borrowed frame -- is planned in section 8** and is what makes the
facade an overlay API rather than a widget-owning one.

Related: `docs/RenderEngine.md` (the bgfx engine), `docs/RendererPlan.md`
(the engine's own build log).


## 1. Decisions taken

**Shape: staged; the simulator's end state is a PERMANENT consumer of
a public draw API** (ruled 2026-08-24, superseding the earlier
absorb-into-the-engine stage 2).

- **Stage 1** -- swap the device. The simulator keeps its widget stack,
  keeps the stencil/depth CSG algorithm exactly as it is, and draws
  through the facade (section 3) instead of raw GL. The last raw-GL
  path in the tree dies, and the simulator inherits bgfx's runtime
  backend selection (GL / Vulkan / Metal / WebGL).
- **Stage 2** -- grow the API, not absorb the consumer. The facade is
  v0 of a public "advanced 3D visual plugin" surface (the Blender `gpu`
  module is the prior art, and the `bgl` deprecation is the cautionary
  tale: raw-GL plugin surfaces die on backend number two). The
  simulator stays on the public API as its flagship advanced user;
  stage 2 extends the API with what it still lacks (scene-depth
  compositing, an overlay hook on ordinary 3D views, Python bindings
  later). Folding the simulator into `BGFXScene` is explicitly off the
  table -- it would leave the plugin API without a serious consumer.

The give-up recorded with that ruling: the free browser/thin-client
simulator that absorption into `SceneDump`/`SceneServer` would have
bought. Immediate-mode plugin draws do not stream; Blender does not
stream addon draws either.

**The sim's SSAO chain is NOT ported; AO comes from the engine as a
built-in effect** (ruled 2026-08-24, superseding the same-day port-it
lean). The engine already carries two AO implementations behind one
entry point (`BGFXView::submitAOResolve`: the classic kernel SSAO and
XeGTAO, selected by `method`); porting the simulator's hand-rolled
chain would have added a third. Instead the API grows its next Blender
layer -- built-in shaders one level up is **built-in effects**: the
consumer supplies the input the effect declares and gets a result
texture back, never seeing the passes in between. AO/GTAO is the first
such effect, the simulator its first consumer, and the simulator's AO
toggle comes back *better* than before (GTAO instead of a 16-sample
kernel SSAO). The visual change at toggle-on is accepted as the
upgrade it is.

What this removes from the port: `shaderSSAO`, `shaderSSAOBlur`, the
SSAO and blur FBOs, the 4x4 noise texture, and the kernel-array
uniform (the fiddliest repack in the whole port). What stays: the MRT
G-buffer and the deferred resolve -- those are structural (the
non-SSAO path resolves from the same G-buffer, and the G-buffer is the
frame cache that lets a repaint skip the CSG).

Staging keeps a working simulator at every commit, and the shader port
(step 2 below) is done once.

**Device access: an immediate-mode facade exported by
`FreeCADRenderer`.**

The simulator cannot call bgfx directly. `src/3rdParty/CMakeLists.txt`
builds bgfx **STATIC on Windows** and SHARED elsewhere, because a bgfx
DLL exports its C99 API and no C++ symbols at all. `bgfx bx bimg` are
therefore `Renderer_PRIVATE_LIBS` in `src/Gui/Renderer/CMakeLists.txt`
on purpose. A second linker of bgfx on Windows would get its **own copy
of bgfx's global state** -- its own device and handle tables, while
`FreeCADRenderer`'s copy holds the real device. That breaks nothing on
Linux and everything on Windows, so it must not be relied on.

So `FreeCADRenderer` grows a small backend-free drawing API, and
`CAMSimulator` links `FreeCADRenderer` only. CAM's files stay in
`src/Mod/CAM/`. The API is obtained through a `RendererLib` virtual
(like `create()`/`warmup()`), not a hard-linked bgfx entry point, so a
second backend (Diligent today, anything later) can implement it --
switching is low-probability, but the interface must not foreclose it.


## 2. What the simulator actually needs

Taken from the code, not from guesswork. `OpenGlWrapper.h` enumerates
every GL entry point used; the counts below are call sites.

**Vertex formats -- three, and that is all.**

| Format | Users | Primitive |
| --- | --- | --- |
| `Vertex` = pos3 + normal3 | `SimShapes`, `StockObject`, `SolidObject` | indexed triangles, `uint16` indices |
| `MillPathPosition` = pos3 + `int SegmentId` | `MillPathLine` | line strip, non-indexed |
| quad = pos2 + uv2 | `SimDisplay` fullscreen passes | triangles, 6 verts, non-indexed |

**Programs -- 15 GLSL 1.20 sources, inlined in `Shader.cpp`, feeding 10
`Shader` instances**: `shader3D`, `shaderInv3D`, `shaderFlat`,
`shaderSimFbo`, `shaderGeom`, `shaderGeomCloser`, `shaderSSAO`,
`shaderSSAOLighting`, `shaderSSAOBlur`, `shaderLinePath`. (GLSL 1.20 is
upstream's macOS 2.1 compatibility floor, and stops mattering the moment
shaderc owns the compile.)

**Uniforms -- 23 locations**, listed in `Shader.h`: the matrices (model,
normalRot, projection, view), the light environment (pos, colour,
ambient, linearity), object colour and colour+alpha, five sampler slots
(albedo, position, normal, ssao, random), the SSAO kernel array, screen
width/height, current segment, start/end, and two booleans
(invertedNormals, ssaoActive).

**Targets** -- one MRT G-buffer (RGBA8 colour, RGB32F position, RGB32F
normal, depth/stencil renderbuffer), plus the SSAO and SSAO-blur
framebuffers and a 4x4 noise texture.

**State** -- colour mask, cull face, depth func (`LESS`/`GREATER`/
`EQUAL`), depth mask, stencil func + op, blend, polygon offset, line
width.

Of that inventory, the SSAO chain -- `shaderSSAO`, `shaderSSAOBlur`,
the SSAO and blur FBOs, the noise texture, the kernel uniform -- is
NOT ported (section 1): the engine AO effect replaces it. It stays
listed above because it is what the code contains today.

**The state is the interesting part.** `MillSimulation` touches GL only
in seven tiny functions (`MillSimulation.cpp:176-235`): `GlsimStart`,
`GlsimToolStep1`, `GlsimToolStep2`, `GlsimClipBack`, `GlsimRenderStock`,
`GlsimRenderTools`, `GlsimEnd`. Every one of them is pure state
setting -- no draws. The draws live in `SimShapes` and friends. bgfx has
no current state (state is an argument of each `submit`), so these seven
functions become writes into a state accumulator that each later draw
folds into its `setState`/`setStencil`. Mechanical, but it touches every
draw site.


## 3. The facade

Two public headers in `src/Gui/Renderer/`, no bgfx types in either.
Handles are opaque 16-bit ids, matching bgfx's own so the implementation
is a cast.

`DrawDevice` -- process-wide resource ownership:

```
static DrawDevice *instance();          // null when no bgfx backend is up
bool available() const;

BufferHandle  createVertexBuffer(const void *, uint32_t bytes, const VertexLayout &);
BufferHandle  createIndexBuffer(const void *, uint32_t bytes, bool int32 = false);
ProgramHandle createProgram(const char *vsName, const char *fsName);
UniformHandle createUniform(const char *name, UniformType, uint16_t num = 1);
TextureHandle createTexture2D(int w, int h, TextureFormat, uint32_t flags, const void *data);
TextureHandle createRenderTexture(int w, int h, TextureFormat, uint32_t flags);
TargetHandle  createTarget(const TextureHandle *colour, int n, TextureHandle depthStencil);
void destroy(<each handle type>);
```

`DrawSurface` -- one per `QOpenGLWidget`, owns a block of bgfx view ids:

```
static std::unique_ptr<DrawSurface> create(QOpenGLWidget *, unsigned numPasses);
bool beginFrame(int width, int height);
void endFrame();                        // context dance + blit into the host FBO

void setPassTarget(unsigned pass, TargetHandle);        // invalid = backbuffer
void setPassRect(unsigned pass, int x, int y, int w, int h);
void setPassClear(unsigned pass, uint32_t rgba, float depth, uint8_t stencil, ClearFlags);
void setPassSequential(unsigned pass, bool);
void setPassTransform(unsigned pass, const float view[16], const float proj[16]);

void setUniform(UniformHandle, const void *, uint16_t num = 1);
void setTexture(uint8_t stage, UniformHandle sampler, TextureHandle);
void setTransform(const float model[16]);
void setState(const DrawState &);
void setStencil(const StencilState &);
void setVertexBuffer(BufferHandle);
void setIndexBuffer(BufferHandle);
void submit(unsigned pass, ProgramHandle);
```

`DrawState` carries colour/depth write masks, depth func, cull, blend,
primitive type, and `depthBias` (the polygon-offset substitute, see
risks). `StencilState` carries func, ref, mask and the
`(sfail, zfail, zpass)` op triple, so GL's calls translate one to one.

Roughly 25 entry points -- sized to the table in section 2, not to bgfx.

**The effect tier.** One layer above the draw calls, the facade offers
engine-internal screen-space effects as services. The first is AO:

```
EffectHandle createEffect(EffectType);          // EffectType::AO first
// input: normal + linear depth in the engine's prepass packing
// (what BGFXView calls aoNormalZ), plus the projection params;
// output: an R8 AO texture the caller samples.
TextureHandle runEffect(EffectHandle, unsigned firstPass,
                        TextureHandle normalZ, const EffectParams &);
```

The consumer renders its input in the declared packing, hands it over
with a range of its own pass ids for the effect's internal passes
(depth pyramid, gen, denoise), and samples the result -- it never sees
the implementation, and the engine keeps one AO implementation for
everyone. The `method` choice (classic/GTAO) and tuning ride
`EffectParams`, defaulting to the engine's own render params.

Implementation lives in a new `BGFXDrawDevice.cpp`, built only under
`BUILD_BGFX`. Without it `instance()` returns null and the simulator
keeps its GL path, which is what makes the staged build-out below safe.

`DrawSurface::create` reuses the existing plumbing rather than
duplicating it: `BGFXRendererLibP::prepare()` for the device,
`reserveBlock()` for the id block (the granule allocator at
`BGFXRenderer.cpp:1004`), and for `endFrame` the context dance already
proven in `BGFXFrame.cpp:5890` -- `widget->doneCurrent()`,
`_BGFXLib.makeCurrent()`, `bgfx::frame()`, `widget->makeCurrent()`,
rebind the host FBO, blit.


## 4. Work breakdown

Each step builds and leaves a working simulator.

1. **Facade skeleton.** `DrawDevice.h/.cpp`, `DrawSurface.h`,
   `BGFXDrawDevice.cpp`; `CAMSimulator` links `FreeCADRenderer`.
   Nothing calls it yet. *Done when:* the tree builds with BGFX on and
   off, and `instance()` is non-null at runtime once a 3D view exists.
2. **Shaders.** The sources minus the two SSAO ones (13) become `.sc`
   files under
   `src/Mod/CAM/PathSimulator/AppGL/shaders/`, compiled into the pack by
   `fc_bgfx_compile_shaders(SHADERDIR ...)` -- that function already
   takes the directory as a parameter, so the sources stay in the CAM
   tree. Repack `vec3` uniforms to `vec4` and the SSAO kernel to a
   `vec4` array. *Done when:* the pack builds for the glsl profile.
3. **Resources.** `SimShapes`, `StockObject`, `SolidObject`,
   `MillPathLine`, `Texture` create facade buffers and textures. Draw
   calls still go through GL.
4. **Targets and fullscreen passes.** `SimDisplay`'s G-buffer becomes
   facade targets (the SSAO and blur FBOs are dropped, section 1); the
   fullscreen resolve draws move over with `ssaoActive` forced off for
   now.
5. **State.** The seven `Glsim*` functions become the state accumulator;
   every draw site submits with explicit state and stencil.
6. **Frame.** `DlgCAMSimulator::paintGL` drives
   `beginFrame`/`endFrame`. This is the commit where bgfx first draws
   the simulator.
7. **The AO effect service.** Lift the engine's AO block
   (`submitAOResolve` and its targets/programs) into an
   instantiable-per-consumer form, expose it through the facade's
   effect tier, adapt the sim's geometry pass to also write the
   prepass packing, and wire the toolbar toggle to it. This is the
   step that restores AO -- as GTAO.
8. **Remove the GL path.** Delete `OpenGlWrapper.{h,cpp}`, the inline
   GLSL in `Shader.cpp`, and drop `${OPENGL_gl_LIBRARY}` and
   `${QtOpenGL_LIBRARIES}` from the `CAMSimulator` target.

Steps 3 and 4 are independent of each other; 5 depends on 3; 7 can
land any time after 6 (the sim runs with AO disabled until it does),
and 8 waits for 7 so the GL build remains the comparison point.


## 5. Risks, each with its substitute

- **`glPolygonOffset` has no bgfx equivalent.** One site:
  `MillSimulation.cpp:368`, `glPolygonOffset(0, -2)` in
  `RenderBaseShape()`. Substitute is a `depthBias` in `DrawState`
  applied as a Z bias in the projection matrix. Compare what the Coin
  side settled for slope-scaled offset.
- **`glLineWidth` has no bgfx equivalent** -- bgfx draws 1px lines,
  full stop. One site: `SimDisplay.cpp:478`, `glLineWidth(2)`, the tool
  path overlay. Either accept 1px or expand the strip to camera-facing
  quads. Decide when step 4 lands; accepting 1px first keeps the step
  small. RESOLVED as the quads: 1px was the stand-in through the
  port, and the expansion landed right after step 8 (section 7).
- **bgfx has no 32-bit integer vertex attribute.** `MillPathPosition`
  carries `int SegmentId` through `glVertexAttribIPointer`. bgfx's
  `AttribType` is `Uint8`/`Uint10`/`Int16`/`Half`/`Float` only, so the
  field becomes a `float`. Exact for segment counts below 2^24, which is
  far past anything a tool path reaches.
- **No 3-channel float render targets.** The RGB32F position and normal
  attachments become RGBA32F.
- **Stencil is per draw, not a mode.** GL enables stencil once and every
  later draw inherits it; bgfx takes it as an argument of each `submit`.
  This is the whole reason step 5 exists, and the reason the accumulator
  has to be threaded to *every* draw site rather than only the ones near
  a `Glsim*` call.
- **Submission order.** The CSG depends on draws landing in submission
  order, so the passes must be `bgfx::ViewMode::Sequential`
  (`setPassSequential`). Getting this wrong gives a plausible-looking
  but wrong image, not an error.
- **Clears are per view.** GL's immediate `glClear` becomes
  `setPassClear` on the pass that runs first.
- **Depth/stencil was a renderbuffer.** bgfx wants a `D24S8` texture
  attachment.
- **`Dummy3DViewer` stays.** It supplies navigation and the camera that
  `SimDisplay::UpdateCamera(const SoCamera &)` reads. Untouched by this
  port.


## 6. Verification

The simulator is a GUI feature, so the check is a real drawn frame, not
a unit test.

- Desktop, real GPU: WSLg Wayland with the d3d12 Gallium driver (see the
  hardware-GL notes in the dev environment docs). Software GL hides
  exactly the class of defect this port can introduce.
- Method: open a CAM job in the simulator, screenshot the GL build and
  the bgfx build at the same camera, and compare. Camera must be pinned
  rather than fitted -- a fit re-frames between runs and the comparison
  stops meaning anything.
- Watch specifically for the CSG going wrong (stock that should have
  been cut still drawn, or cuts eating too much), which is what a
  mis-ordered pass or a dropped stencil state looks like.


## 7. Execution record (2026-08-25)

All eight steps landed in one pass, each verified before the next.
What the plan did not know in advance:

- **More of the GL path was dead than listed.** The 2DTex shader pair
  was declared but never compiled into a program; `Texture` and
  `TextureLoader` had no callers at all; `StartCloserGeometryPass` and
  `RenderLightObject` were never called (so the GeomCloser program was
  not ported either -- its projection trick lives on as the base-shape
  pass's depth bias). `lightLinear`, the screen-dimension uniforms and
  `UpdateStartEnd` crossed into no ported shader.
- **The segment-id attribute rides TexCoord0**, not a second texcoord
  slot: the facade's attribute enum is sized to its consumers, and the
  line shader reads `a_texcoord0.x`.
- **The base-shape polygon offset became a pass**, not a DrawState
  field: a backend pass has one projection, so `SimPassBaseShape`
  draws with a slightly-closer copy of it (the GeomCloser scale
  factor). `DrawState::depthBias` stays declared but unimplemented.
- **The path-line pass needs its own colour+depth target.** A
  one-output fragment shader stores undefined values into MRT
  attachments it does not declare; the AO effect made that visible as
  a broad occlusion streak along the rapid lines. The line pass now
  targets colour+depth only, sharing the same attachments.
- **The AO input is a fourth G-buffer attachment**: the geometry pass
  writes the engine's prepass packing (octahedral viewer-facing
  normal + positive linear view depth) alongside its own outputs.
  `octEncode` is copied into the CAM shader because the CAM shader
  tree compiles without the engine's include directory.
- **The effect implementation duplicates the engine's submit sequence**
  (BGFXViewEffects.cpp) rather than refactoring BGFXView's members
  into a shared chain -- zero regression risk to the engine; the two
  copies are cross-referenced and must be kept in step.
- **A `UseFacadeRender` preference existed for steps 6-7 only**, as
  the A/B switch (the render path selection always warms the backend,
  so no session would otherwise reach the GL comparison leg). It was
  deleted with the GL path.

Verification ran as planned, plus a scripted harness: a probe drives
`CAMSimulator.PathSim` directly (tool, G-code, stock), sets the stage
slider to 60% and screenshots under xvfb. The facade image came out
pixel-identical to the GL one except the tool-path overlay, where
every differing pixel was the documented 2px-to-1px line-width
substitute; AO on-vs-off is pure darkening; and the GL-path deletion
left the facade output bit-identical. On real hardware (WSLg d3d12)
the surface presents and runs clean -- Wayland cannot screen-grab, so
the pixel evidence is the llvmpipe A/B.

Fixed on the way: `View3DInventorViewer::addViewProvider` crashed on
the document-less Dummy3DViewer (first time the AppGL simulator ever
ran in this fork), and the AO effect's first implementation cached
`c_str()` of a temporary shader path.

**Post-port: the tool-path width came back.** The 1px stand-in was
replaced by screen-facing quads: each strip segment becomes two
triangles whose vertices carry both endpoints plus (segment index,
side), and the vertex shader offsets them half the line width along
the segment's screen-space perpendicular (u_simParams.w, in pixels --
half of GL's glLineWidth(2)). Non-indexed on purpose (16-bit indices
would overflow on long paths), culling off for the pass (quad winding
follows each segment's screen direction). Verified against the
GL-path reference frame: the lines are back at 2px in the same rows
and columns; what remains differing is sub-pixel placement and
endpoint caps, which GL's own wide-line rasterization rule decided
differently.


## 8. Stage 2 -- the borrowed frame

Stage 1 gave the simulator a device. Stage 2 gives it a **place in
somebody else's frame**, which is the one thing an immediate-mode
plugin API cannot be useful without: a consumer that can only own a
whole widget can never be an overlay, and can never sort against the
document's own geometry.

The ruling in section 1 named three extensions -- scene-depth
compositing, an overlay hook on ordinary 3D views, and Python
bindings later. Read against the code, the first two are **one
mechanism seen from two sides**. Compositing means the consumer's
draws and the scene's draws share a depth buffer; sharing a depth
buffer means drawing into the host's target; drawing into the host's
target means submitting inside the host's frame, because a bgfx
frame boundary is process-wide and only one party may cross it. So
there is one thing to build -- a `DrawSurface` that attaches to an
engine view instead of owning a `QOpenGLWidget` -- and both items
fall out of it. Python bindings stay deferred: freezing a scripting
surface before the C++ shape has a second user would be the wrong
order.

### 8.1 What the simulator gets out of it

`Dummy3DViewer` is a full `View3DInventorViewer` that **never
paints**: `discardPaintEvent_` defaults true (`Dummy3DViewer.h:49`)
and only the disabled `#else` side-by-side branch at
`ViewCAMSimulator.cpp:96` turns it off. It exists for its camera and
its navigation, and it carries two `TopoShapeViewProvider`s -- stock
and base -- that are built, fed by `setStockShape`/`setBaseShape`,
and then never rendered. The simulator draws its own stock through
the CSG and the base shape not at all.

With a borrowed frame that viewer becomes the real one. The document
geometry -- the job's base shape, fixtures, whatever else is in the
document -- draws through the engine with its PBR, shadows and GTAO,
and the simulator's carved stock sorts against it by depth instead
of being pasted over it by Qt's widget stacking. The three-widget
`QStackedLayout::StackAll` stack collapses: `GuiDisplay` stays a
plain Qt toolbar over the top, and `DlgCAMSimulator` stops being a
`QOpenGLWidget` at all.

That is the flagship-consumer argument made concrete. Nothing about
it absorbs the simulator into `BGFXScene` -- the CSG stays the
simulator's, in the simulator's tree, drawn through the public API.

### 8.2 Where a consumer's passes go

bgfx orders draws by **view id**, not by submission order, so a
consumer that must interleave with the scene cannot simply take a
block from the granule pool the way stage 1's surface does
(`BGFXDrawDevice.cpp:1097`): a pool block lands wherever there is
room, which is nowhere in particular relative to the host's ids.
The ids have to come out of the host's own block, at the right point
in the host's own order.

The engine already has exactly the machinery for that. A `BGFXView`
declares which passes it will use each frame (`declPass`/`declPasses`,
`BGFXFrame.cpp:3780`), sizes its block to that count
(`passesNeeded`), and hands the live passes consecutive ids **in
enum order** (`mapPasses`, `BGFXRendererP.h:6008`). An undeclared
pass costs nothing. So the consumer block is a new run in the
`PassView` enum, declared live only while a consumer is attached:

```
ViewSectionCapTransp,
ViewConsumer0,      // attached DrawSurface passes (docs sec 8)
...                 // 16 of them, like the bulb-shadow block
ViewConsumer15,
ViewBloomBright,
```

Sixteen because the simulator needs thirteen (four draw passes --
scene, base shape, path, resolve -- plus nine for the AO effect) and
a round block leaves headroom for the next consumer. `mapPasses`
needs no change beyond the enum growing; `passesNeeded` already
counts only what is marked.

**Placement: after `ViewSectionCapTransp`, before `ViewBloomBright`.**
That puts consumer output inside the scene composite -- bloom, the
user post stage, debug visualization, and idle accumulation all see
it -- while on-top geometry, the selection highlight and the overlay
block still draw over it. Sorting against transparent scene geometry
is given up (the consumer draws after the WBOIT resolve, so it
occludes transparents rather than blending with them); the
simulator's stock is opaque, and a documented limit beats a second
insertion point.

### 8.3 The API

Two additions, both in the backend-free headers.

`Renderer` grows a consumer registration -- a virtual, like every
other engine feed, so a second backend can implement it:

```
class FrameConsumer {
public:
    virtual ~FrameConsumer();
    /// Passes wanted inside the host frame, <= kMaxConsumerPasses.
    virtual unsigned framePasses() const = 0;
    /// Submit into the host's frame. Called once per host frame,
    /// after the scene composite and before the overlays; the
    /// surface's passes are live only for the duration of the call.
    virtual void drawFrame(DrawSurface &surface) = 0;
};

virtual void setFrameConsumer(FrameConsumer *consumer) { }
```

`DrawSurface` grows the host-frame half, plus a second way to make
one:

```
/// A surface attached to \a renderer's view: its passes are ids
/// inside that view's frame, and the host owns the frame boundary.
/// Null when the renderer has no device (render cache is not the
/// renderer mode, no backend built) -- the caller falls back to
/// create(widget, n).
static std::unique_ptr<DrawSurface> attach(Renderer *renderer,
                                           unsigned numPasses);

bool attached() const;          // false = owns a widget (stage 1)
TargetHandle hostTarget() const;  // the host's scene colour+depth
TextureHandle hostColor() const;
TextureHandle hostDepth() const;
void hostSize(int &w, int &h) const;
/// True when the host's scene colour holds LINEAR light (the
/// colour-managed RGBA16F target). A consumer that shades in
/// display space must encode before writing -- see the risk below.
bool hostLinearColor() const;
```

On an attached surface `beginFrame`/`endFrame` are no-ops that return
true: the host has already begun the frame, and `bgfx::frame()`
belongs to it. Everything else -- `setPassTarget`, `setState`,
`setStencil`, `submit`, `runEffect` -- behaves identically, which is
the point: **the consumer's drawing code does not know which flavour
of surface it holds.** Only target selection and the resolve differ.

Implementation: `BGFXDrawSurface` splits its id source (pool block vs
host `idMap`) and its frame boundary (context dance + blit vs
nothing). The rest of the class is unchanged.

### 8.4 Compositing without giving up the CSG

The simulator is deferred: geometry into its own G-buffer, then a
fullscreen resolve. The naive reading of "share the depth buffer" is
to hang the sim's G-buffer off the host's depth attachment so the
CSG depth-tests against document geometry directly. That is wrong
twice: the CSG's stencil work would collide with whatever the engine
put in the shared stencil, and a pass cannot read the depth it is
writing.

It is also unnecessary. The CSG is self-contained -- stock minus
tools, correct in isolation -- and occlusion against the document
only has to be right **at composite time**. So:

- the G-buffer, its depth/stencil, and all seven `Glsim*` states stay
  exactly as stage 1 left them, private to the consumer;
- only the **resolve** changes. Instead of a plain fullscreen draw
  into the surface's own backbuffer, it targets `hostTarget()`,
  writes `gl_FragDepth` from the sim's own depth, and runs with
  depth test LESS and depth write on.

Pixels where the simulator's stock is nearer than the document win
and stamp their depth; pixels where it is not are left to the scene;
and the passes downstream (on-top, highlight, overlays) see a depth
buffer that includes the stock. One shader change and one target
change, and the sim's own pipeline is untouched.

The G-buffer must size to `hostSize()`, not to the widget:
`Render_EffectResolution` and the sub-view banks both mean the host's
scene target is not always the widget's pixel size.

### 8.5 Work breakdown

Each step builds and leaves a working simulator.

1. **The attached surface.** `FrameConsumer`, `Renderer::
   setFrameConsumer`, the `ViewConsumer0..15` block and its
   declaration, and the attached `BGFXDrawSurface` flavour. *Done
   when:* a probe consumer draws one triangle inside a real 3D view's
   frame, on the engine's ids, with no second `bgfx::frame()`.
2. **Host accessors.** `hostTarget/hostColor/hostDepth/hostSize/
   hostLinearColor`. *Done when:* the probe triangle depth-tests
   against document geometry and is correctly encoded in both the
   RGBA8 and the RGBA16F host configurations.
3. **The simulator attaches.** `DlgCAMSimulator` becomes a
   `FrameConsumer` rather than a `QOpenGLWidget` when the viewer has
   a renderer; `Dummy3DViewer` paints; the widget stack loses a
   layer. The resolve still writes into a private target and blits,
   so the image is stage 1's. *Done when:* the simulator runs
   attached and looks unchanged.
4. **The composite resolve.** `gl_FragDepth` + host encoding +
   `hostTarget`. This is the commit where the stock and the document
   first occlude each other.
5. **Camera and input.** `SimDisplay::UpdateCamera` reads the host
   camera directly; the copy through `MDIViewWithCamera` goes away
   where it becomes redundant.
6. **Fallback and cleanup.** Keep `create(widget, n)` as the path for
   a viewer with no renderer (render cache not in the renderer mode),
   document which path runs when, and delete what died -- the
   `#if 1`/`#else` debug branch, `discardPaintEvent_`, and the stock
   view provider if the engine now draws it.

Steps 1 and 2 are the API; 3 through 5 are the consumer; 6 waits for
all of them.

### 8.6 Risks, each with its substitute

- **Double-encoded colour.** The host's scene colour is RGBA16F
  holding linear light whenever colour management is on (`hdrScene`,
  `BGFXViewLifecycle.cpp:823`), and the output transform encodes it
  at present time. The simulator shades in display space. Writing its
  colour straight in would send it through the transform twice.
  *Substitute:* `hostLinearColor()` and a decode in the resolve
  shader; the standalone path leaves it alone.
- **MSAA host target.** The scene target carries the view's MSAA
  flags. Draws into it are fine, but `gl_FragDepth` defeats early-z
  and forces per-sample evaluation on some drivers. *Substitute:*
  accepted -- the resolve is one fullscreen pass, and correctness
  beats the early-z it costs.
- **No host renderer.** Render cache outside the renderer mode means
  `getExternalRenderer()` is null and there is no frame to borrow.
  *Substitute:* the stage 1 standalone surface stays, and the
  simulator picks its flavour at construction. This is why step 6
  keeps rather than deletes it.
- **View-id budget.** An attached consumer adds up to 16 ids to the
  host view's block. *Substitute:* they are declared only while a
  consumer is attached, and `reserveBlock` already fails soft (the
  view sits the frame out and Coin composites).
- **Re-entrancy.** `drawFrame` runs inside the engine's frame. A
  consumer that touched scene state, resized, or asked for a repaint
  from there would corrupt the frame in progress. *Substitute:* say
  so in the header, and give the surface nothing that could -- the
  attached flavour exposes no frame-boundary call at all.
- **Stencil sharing.** The consumer's private depth/stencil keeps the
  CSG's stencil traffic out of the host's buffer. *Substitute:* none
  needed; this is why 8.4 keeps the G-buffer private.

### 8.7 Verification

The stage 1 harness extended (`scratchpad/simab/sim_ab.py`): the same
scripted `CAMSimulator.PathSim` run, screenshotted under xvfb, in
three legs -- standalone (stage 1's image, the regression baseline),
attached with no document geometry (must be pixel-identical to it
modulo the host's colour transform), and attached with a base shape
visible (the new case: the stock must occlude and be occluded).
Real-GPU confirmation on WSLg d3d12 as before, where the evidence is
that the frame presents and the depth ordering is right rather than a
pixel compare.


### 8.8 Execution record (2026-08-27, steps 1-3 landed)

**Steps 1 and 2 are done and verified** (`6479247e61`). What the plan
did not know in advance:

- **The renderer owns the attached surface, not the consumer.** The
  plan's `DrawSurface::attach(Renderer*, n)` factory became
  `Renderer::setFrameConsumer(FrameConsumer*)`: the renderer builds the
  surface, sizes it to the consumer's pass count, binds this frame's
  ids and target for the duration of one `drawFrame` call and unbinds
  after. Lifetime, id validity and frame scope then have one owner
  instead of two, and a consumer that squirrelled the surface away can
  submit nothing outside the callback. A consumer's *drawing* code is
  still identical in both flavours, which is what the plan actually
  wanted -- the simulator's `drawFrame(DrawSurface&)` is called by the
  host when attached and by its own `paintGL` when not.
- **`hostTarget()` reads correctly in both flavours** because an
  invalid `TargetHandle` already meant "this surface's backbuffer".
  `setPassTarget(pass, surface.hostTarget())` therefore needs no test
  of which flavour it is -- which is what makes one drawing path
  possible.
- **! A pass with no stated transform inherits a stale one.** A
  backend view's transform is sticky, and an attached surface's ids
  are reassigned every frame from whatever is live, so an id that
  carried the scene camera last frame would apply it to a clip-space
  fullscreen quad this frame. Every pass now states its transform,
  identity included.
- The pass block is `ViewConsumer0..15`, sixteen, declared live only
  as far as the consumer asked. `NumConsumerViews` is counted between
  enum entries like the overlay block, so inserting a pass into the
  run cannot desync it.
- Verification was a probe consumer on a real `BGFXRenderer` rather
  than the simulator, so that the API and its first consumer could
  fail separately. Worth keeping that order.

**Step 3 is DONE** (`6c55968dde`). The simulator attaches, the widget
stack switches (`Dummy3DViewer` paints, `DlgCAMSimulator` hides),
redraw requests go to the host's render manager instead of the hidden
widget, and the viewer's stock/base view providers stop mirroring the
simulator's shapes -- they would otherwise draw the stock UNCUT over
the carved one. Verified attached and standalone side by side under
xvfb: same stock, same carved channel, same tool, same path line.

**! The mirrors are also what the camera was framed on.** Turning them
off empties the viewer's scene graph, and `viewAll()` frames the scene
graph -- so the camera the simulator reads (`SimDisplay::UpdateCamera`,
off `Dummy3DViewer`) was left sitting on the origin at unit distance
with the stock entirely outside its frustum. The simulator then drew a
perfectly correct picture of nothing: an empty G-buffer, a resolve of
an empty G-buffer, a composite of that. Every visible symptom was
downstream of one wrong matrix.

The fix is `ViewCAMSimulator::viewFit()`, which every view fit now goes
through: it frames the union of the viewer's own scene bounds and the
bounds of the shapes the SIMULATOR draws itself
(`DlgCAMSimulator::simulationBoundBox`), so it is right whichever side
is holding the geometry, and it resets the height angle the way
`viewAll()` does. The general lesson for the rest of stage 2: **moving
a shape out of the viewer's scene graph moves it out of every service
that reads that scene graph**, and framing is only the first of those.

**The defect recorded here on 2026-08-27 was not real.** The previous
session's rule -- "a consumer draw sampling three or more distinct
attachments of a framebuffer it rendered to earlier in the same host
frame produces nothing" -- did not survive re-testing. It came from a
probe that was confounded: with the camera wrong the G-buffer was
empty in *every* attached run, so what the probe was actually varying
was not the thing under test. Re-tested directly, each claimed
ingredient came apart:

- a consumer pass CAN clear and draw into the host's scene target from
  inside the host frame, and it reaches the screen;
- the attachment count of the consumer's own G-buffer (1, 2 or 4)
  changes nothing;
- every consumer draw is submitted and counted by bgfx (`numDraw` is
  identical in working and failing runs), and a bgfx view-switch trace
  shows the clears landing on the right framebuffer;
- bgfx is built with `BX_CONFIG_DEBUG=1`, so `GL_CHECK`/`BX_ASSERT` is
  live: there are no GL errors anywhere in the frame.

Worth keeping from that search: the harness leg that gives hardware GL
*and* a screen grab at once, which the WSLg Wayland route never did --
`GALLIUM_DRIVER=d3d12 MESA_LOADER_DRIVER_OVERRIDE=d3d12
LIBGL_ALWAYS_SOFTWARE=0` under `xvfb-run`.

Step 3 also lands the composite pass the plan did not name: the
deferred resolve writes into the simulator's own colour target and one
textured quad (`SimPassComposite`) carries that image into whatever
the surface composites into -- its own backbuffer standalone, the
host's scene target attached. Exactly one texture crosses into the
host's frame, and that quad is where step 4's `gl_FragDepth` write
belongs.

**Open at the end of step 3, closed by it:** attached output was
visibly lighter than standalone. That is the double-encode risk of
section 8.6 arriving on schedule -- the host's scene colour is
RGBA16F holding linear light,
the simulator shades in display space, and the output transform
encodes the result a second time. `hostLinearColor()` was already on
the surface; the decode went into the composite shader (`b3f82b620b`).

### 8.9 Execution record: step 4 (2026-08-27)

The colour half landed first (`b3f82b620b`, the decode above). The
depth half is `7b148e9cdf` plus `ec78d940e1`, and both were needed
before anything could be seen: a consumer that writes depth into a
frame containing nothing else to sort against looks exactly like one
that does not.

**The simulator's own depth buffer could not be handed over.**
Section 8.4 says the resolve "writes `gl_FragDepth` from the sim's own
depth" as though the value were already in the right space. It is not.
`SimDisplay::UpdateCameraProjection` derives near and far from
`mMaxStockDimension`, not from the camera, so the simulator's window
depth is in a private frustum that has nothing to do with the host's.

What the two DO share is view space -- both read the same camera --
and the G-buffer already holds a view-space position per texel. So the
composite transforms that position into the host's clip space and
writes the result. The matrix is built on the CPU as
`hostProj * hostView * inverse(simView)`, which is exact whatever the
two cameras turn out to be, rather than assuming they agree.

That needed one addition the plan did not list: **`DrawSurface::
hostCamera`**, the host's view and projection for the bound frame,
riding the frame bind next to the target and its size. It is the
general form of what section 8.5 step 5 wants too. `DrawDevice::
homogeneousDepth` came with it, so the clip convention is answered by
the device that will run the draw rather than by the shader testing
its own language.

**! The position attachment needed a written-marker.** `.w` was 0
everywhere; the clear is 0 too, so "no geometry here" and "geometry at
the view-space origin" were the same texel. It is now 1 where the
geometry pass wrote, and texels without it -- the tool path over the
background, which draws into a target sharing only colour and depth --
take the far plane.

**The base shape moved to the engine** (`ec78d940e1`), which is what
made the depth visible at all and is the thing section 8.1 promised.
Step 3's reason for turning the mirror view providers off applies only
to the stock: the material removal IS the simulator's rendering and
there is no mesh of the result to hand over. The base shape has no
such claim on it. So the mirror decision splits per shape, with
`MillSimulation::SetBaseDrawnByHost` as the other half.

**! A view provider with no material draws black through the
renderer.** `TopoShape::exportFaceSet` writes a material only when it
is given face colours, and `TopoShapeViewProvider` gave it none, so
the shape inherited whatever the traversal was carrying -- nothing.
It now carries an `SoMaterial` set to the colour the simulator draws
the same shape in, so the picture does not jump when a shape crosses
from one renderer to the other.

**Verification.** The harness grew two shapes for it (`SIM_BASE=2`,
`SIM_BASE=3`) and a `SIM_BOTH=1` that clicks `stockModelButton` twice
-- the base is not visible by default, which is why the first attempts
photographed nothing and proved nothing.

The result that settles it is the piercing bar: a bar that starts in
front of the stock, runs through it, and comes out behind. Attached,
the front stub is visible, the middle is buried, the rear stub is
visible, and the bar shows through where the tool has carved the
channel down past it. Ordering alone cannot produce that picture --
the depth VALUES have to be right, or the entry lands in the wrong
place and the channel patch disappears. Against the standalone leg
(where the simulator draws the bar itself) every occlusion boundary
lands within one pixel.

**Known consequence, accepted.** The tool path's hidden-line pass --
drawn at 10% alpha where the path runs inside the material, so the
operator can see where the tool goes -- is covered wherever host
geometry is in front of it. The simulator composites one finished
image, so the host occludes all of it at once, translucent overlay
included. Recovering it would mean handing over depth per pass rather
than one image, which is a different design from 8.4's.

### 8.10 Execution record: steps 5 and 6 (2026-08-27)

**Step 5 needed no code.** The plan expected `SimDisplay::UpdateCamera`
to be reading a copy; it is not, and never was.
`DlgCAMSimulator::updateCamera` reads `mDummyViewer->getCamera()`
every frame -- the host viewer's own Coin camera, directly. The
`MDIViewWithCamera` traffic is not a per-frame copy either: it is the
string form of a camera, used twice, both one-shot. `initCamera`
seeds the simulator's camera from the active 3D view when the window
opens, and `cloneFrom` carries it across the widget rebuild on
dock/undock. Neither is redundant, and deleting them would lose the
simulator's opening view.

Input was checked rather than assumed, because "the toolbar is a
full-size overlay stacked on top of the viewer" looks like it should
swallow every mouse event. It does not: `GuiDisplay::resizeEvent`
already masks the widget to `childrenRegion()`, so only its controls
hit-test, and what is under the middle of the viewport is the
viewer's own viewport widget. A drag delivered to the viewer orbits
the camera.

**! A probe that sends synthetic mouse events must send them to the
QGraphicsView, not to its viewport child.** `QApplication::widgetAt`
answers with the viewport, which is the right answer for where a real
mouse lands, but `sendEvent` to it bypasses the scroll-area routing
that turns a mouse event into navigation -- so the camera does not
move and the simulator looks broken when it is not. Send to
`stack->widget(2)` (gui, dlg, viewer) with the middle button, which
is what the CAD navigation style rotates on.

**Step 6.** The `#if 1`/`#else` side-by-side debug branch is deleted.
The other two candidates the plan listed are NOT dead, and each now
says so where it lives:

- `discardPaintEvent_` is the standalone path. The viewer must not
  paint when the simulator owns the picture, or an empty scene lands
  over the simulator's output.
- `DrawSurface::create(widget, n)` likewise -- kept deliberately, and
  `updateHostAttachment` now documents which of the two paths runs
  when and what each costs.

**Open, for a ruling rather than a guess: the stock view provider now
renders in no configuration.** Attached, its mirror is off because
only the simulator can draw the carved stock. Standalone, it is fed
but the viewer never paints. With the side-by-side branch gone there
is no third case. It is still maintained coherently (fed, cloned,
counted in the viewer's bounds), and the plan's condition for
deleting it -- "if the engine now draws it" -- is not met, so it
stays until someone decides whether an uncut-stock view is wanted.
