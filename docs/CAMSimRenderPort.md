# CAM simulator render port

Porting the CAM simulator's rendering off raw OpenGL. **Stage 1 is
COMPLETE (2026-08-25)**: all eight steps landed; the simulator draws
through the facade, and section 7 records what the execution added to
that plan. **Stage 2 -- the borrowed frame -- is COMPLETE
(2026-08-27)**: section 8, with execution records in 8.8-8.10; the
simulator draws inside a host view's frame and sorts against the
document by depth. The **legacy raw-GL renderer is a permanent second
path** (section 9), not a stage of the port. **Stage 3 -- the phased
contract -- is DESIGNED in section 10**, not yet built; section 11
holds the requested cut-shape-in-the-normal-view feature that follows
it.

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
insertion point. (Stage 3 adds that second insertion point after all
-- section 10 -- once there is a second thing to place.)

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
than one image, which is a different design from 8.4's. (Recovered by
stage 3, section 10 -- though not by depth-per-pass.)

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

**The stock view provider stays.** An earlier draft of this section
said it "renders in no configuration", which conflated two objects.
What is on SCREEN is the CSG's output, and that always includes the
stock: `mPathStep` starts at -1 and every segment loop in
`RenderSimulation` is bounded by it, so at the start -- and at any
rewound point, since the simulator is a movie player -- the CSG draws
the stock and subtracts nothing. The uncut stock is on screen there.

What never paints is `Dummy3DViewer::stockViewProvider`, the second
Coin-side copy the ENGINE would draw: its mirror is off when attached
(only the simulator can draw the CARVED stock -- the material removal
IS its rendering, and there is no mesh of the result), and standalone
the viewer does not paint at all.

That distinction is also a design lead for section 10. At
`mPathStep == -1` the CSG result IS exactly the uncut stock, so the
engine could legitimately own the stock in that state and draw it with
the document's PBR, shadows and GTAO, the simulator standing down
until the first cut -- at the cost of a hand-back when the first cut
lands. So the provider is a live asset for the next contract rather
than a leftover, and it is kept on that ground.

## 9. The legacy GL renderer is permanent, not a transition

Stage 1 step 8 deleted the raw-GL path on the reasoning that "the
render path selection always brings the backend up in this fork", so a
missing backend meant a blank simulator "as designed".

**That reasoning does not survive the build options.** `BUILD_BGFX`
defaulted OFF at the time, and so did `BUILD_CAM_SIMULATOR_GL`
(`InitializeFreeCADBuildOptions.cmake`; both default ON since
2026-08-28 -- RULED -- with a graceful degrade when the bgfx
submodule is not checked out). A build without the backend has
nothing to draw through, and a session that has never warmed one
does not either. What step 8 described as designed behaviour is
just a simulator that does not work.

So the deletion is reverted (`2dc2abf6e6`) and the GL renderer is a
permanent second path, not a stage of the port. Both are supported.

### 9.1 Which one draws

`DlgCAMSimulator::useLegacyGL()`:

- **Legacy GL** when `Render::DrawDevice::instance()` is null -- bgfx
  not built, a backend that would not start, or nothing has warmed one
  -- or when the `Mod/CAM` `ForceLegacyGLRender` preference asks for
  it. The preference is what makes the two comparable on one machine,
  and gives a user whose driver the backend dislikes somewhere to
  stand. It takes effect when the simulator is reopened: a running
  simulation holds buffers built for the path in force when it was set
  up.
- **The facade** otherwise, standalone or attached.

A forced-legacy session never attaches to a host frame: legacy GL owns
the widget's own context and cannot draw inside somebody else's.

### 9.2 ! The two are mutually exclusive, which they were not before

The `UseFacadeRender` switch of steps 3-7 let BOTH paths run every
frame -- that is what made the A/B comparison possible, and it was
harmless while the facade owned its own widget.

It is **not** harmless now. Attached, the simulator draws inside the
HOST's frame (section 8), and a stray `glEnable` or `glDepthFunc`
there desynchronises the backend's state cache -- corrupting the
document's rendering, not just the simulator's.

So every GL call in the shared files sits behind
`SimDrawContext::legacyGL`. The flag is set by the frame driver
**before resources are built** as well as before the draws, because
the buffers and shaders are made lazily from `updateResources()` and
each path must build only its own. `SimDrawContext` was always the
place the two paths meet; this is one more field of that.

Two things that are safe without a guard, and say so where they live:
`GLDELETE` tests the id, and `Shader::Destroy` tests the program --
both no-ops for resources the legacy path never created.

`Texture` and `TextureLoader` stay deleted. They had no callers even
before the port, so restoring them would restore dead code rather than
rendering.

### 9.3 Verification

Three legs of the stage 2 harness on the piercing-bar scene
(`SIM_BASE=3 SIM_BOTH=1`, plus `SIM_LEGACY=1` for the third):

- attached facade -- pixel-identical to before the restoration;
- standalone facade -- pixel-identical;
- legacy GL vs the facade -- 657 differing pixels of 465600, every one
  of them on the tool-path lines. That is the documented
  `glLineWidth(2)` versus screen-facing-quads substitute (section 5),
  and nothing else in the picture moved.

**! The audit that matters is not just `gl*(` calls.** The first pass
guarded those and left `CurrentShader->UpdateModelMat` in
`Shape::Render` unguarded -- a null dereference the moment the facade
drew, because nothing had compiled a Shader. Grep for the shader
objects and `CurrentShader` too.

## 10. Stage 3 -- the phased contract (designed 2026-08-27)

Stage 2's contract carries exactly ONE finished RGBA8 image and one
depth per pixel across into the host frame. That bought a clean, cheap
boundary, and it costs two things that are now recovered rather than
accepted:

**The tool path's hidden-line pass.** `MillSimulation::RenderPath`
draws the path twice -- `depthFunc LESS` at full alpha for the visible
run, then `depthFunc GREATER` at alpha 0.1 for the part that runs
INSIDE the material, so the operator can see where the tool goes. It
is a deliberate x-ray. Because the simulator composites one image, the
opaque stock and that translucent overlay occlude as a single layer,
so host geometry in front covers both at once (section 8.9).

**Transparent scene geometry** (section 8.2). The consumer draws after
the WBOIT resolve, so it occludes transparents rather than blending
with them.

The first framing of the fix was "depth per pass" -- hand a depth
buffer over per layer so the host can interleave. **Rejected on
audit.** Both losses turn out to be PLACEMENT problems, and the
placement machinery is nearly all built already.

### 10.1 The two facts that decide the design

1. **The OIT framebuffer shares the scene depth attachment**
   (`BGFXViewLifecycle.cpp:1190`, attached read-only). If the sim's
   composite runs at a pass id BEFORE `ViewTransparent`, the WBOIT
   accumulation depth-tests against the stock automatically. Nothing
   new crosses the boundary. And the C++ call site of `drawFrame` is
   reading order only -- the ids place the draws -- so an early
   consumer block costs no restructuring of the frame path.
2. **After the composite stamps the stock's depth, the host depth
   buffer holds min(document, stock)** -- exactly the surface the
   x-ray wants to test against. The facade already has per-pass
   transforms and `hostCamera()`; the sim's world space IS the
   document's (the depth transform is
   `hostProj * hostView * inverse(simView)`); and the line vertex
   shader reads only `u_viewProj` and `u_viewRect`, so it runs
   unchanged under the host's camera. Re-submitting the path's two
   draws in HOST clip space against the merged depth reproduces the
   x-ray -- and extends it: the path now also shows faintly through
   the engine-drawn base (which stage 2 silently lost from the
   GREATER pass's reach) and through any other document geometry.

What stays rejected: carrying per-layer depth textures across the
boundary, or joining the consumer's translucent output into WBOIT
itself. Far heavier, and a 10%-alpha line overlay does not justify it.

Prior art agrees. Blender's viewport draw handlers solve the identical
problem with placement tags (`PRE_VIEW` / `POST_VIEW` / `POST_PIXEL`),
not with depth layers -- and since the facade's ambition is a
`gpu`-module workalike (`docs/ExternalRenderer.md` section 7), matching
Blender's mechanism here is a point in favour, not a convenience.

### 10.2 The contract: phase-tagged passes

A consumer's passes split into two runs, each a block in the
`PassView` enum, declared live only while a consumer is attached (the
view-id budget is untouched otherwise):

- **The scene run**, `ViewConsumerScene0..15` -- sixteen, sized like
  the old block -- placed after `ViewOutline`, before `ViewCaustics`
  and `ViewVolApply` (ruled: before `ViewVolApply`). Everything the
  frame composes from there on sees the consumer's opaque output and
  its depth: water caustics land on the stock, the volumetric fog
  covers it, water and glass surfaces sort against it, WBOIT blends
  transparent document geometry over it, and bloom, the user post
  stage, debug visualization and idle accumulation see it as before.
- **The overlay run**, `ViewConsumerOverlay0..3` -- the OLD block's
  position (after `ViewSectionCapTransp`, before `ViewBloomBright`),
  shrunk from sixteen to four now that the bulk of a consumer's work
  belongs in the scene run. This is where translucent output that must
  read the finished scene depth goes.

The API change is one virtual next to `framePasses()`:

```
/// Of framePasses(), how many TRAILING passes belong in the overlay
/// run -- drawn after the transparent composite, against the frame's
/// finished depth. The rest are the scene run, drawn where the scene
/// is still being composed (before volumetrics and transparency).
/// Ordering within each run follows pass index, as before.
virtual unsigned overlayPasses() const { return 0; }
```

Passes `0 .. N-overlay-1` are the scene run, the trailing `overlay`
are the overlay run. The default is all-scene, which is where a
consumer's composite belongs; the one existing consumer moves in the
same commit. `bindFrame`'s ids array simply fills from the two enum
runs -- the surface's pass numbering, targets, clears, sequential
flags and transforms are untouched, so the consumer-side facade does
not change shape at all.

While `bindFrame` is being touched it folds its ten positional
parameters into a bind-info struct; stage 3 adds to them, and the
signature is past the point where positions are readable.

### 10.3 The composite writes depth in BOTH flavours

Standalone, `hostCamera()` degenerates to the sim's own view and
projection (the depth transform collapses to `simProj`), and the
composite writes depth there too -- `LEqual`, depth write on, into a
target whose clear already includes depth. One code path instead of a
`writeDepth` fork, and the property stage 1 bought -- the sim's
drawing code does not know which flavour it holds -- extends to the
depth write.

That is what lets the path passes be uniform: they always draw after
the composite, against the target's depth, under the target's camera
-- the host's when attached, the sim's own when not.

### 10.4 The path becomes an overlay-run draw against the merged depth

`SimPassPath` leaves the G-buffer run entirely. `mRPathTarget` (the
colour+depth-only alias target) is deleted, and the G-buffer cache
goes back to holding geometry only -- the path is redrawn every frame
as two draws in the overlay run, visible (`LESS`, full alpha) then
hidden (`GREATER`, alpha 0.1), in the target camera's clip space. The
sim's private near/far stops mattering for this pass: the path tests
the merged depth directly.

Consequences, each deliberate:

- **Ruled: the x-ray widens.** Against min(document, stock) the faint
  pass shows the path through ANY occluder -- stock, base, fixture,
  any document geometry -- not only "inside the material". Accepted
  as the better x-ray, not merely a recovery.
- **The path's colour becomes exact.** It used to be baked into the
  G-buffer albedo and ride through the deferred lighting and its
  gamma; that modulation was incidental to the
  depth-test-in-the-G-buffer arrangement, not a design. It now draws
  in its stated colour, with the `hostLinearColor()` decode the
  composite already does (`fs_camsim_line` grows the same decode).
  The A/B diff class against legacy GL grows accordingly: path pixels
  may differ in colour as well as in the width substitute.
- **Residual, documented the way 8.2 was:** the translucent overlay
  itself still cannot blend correctly with transparent DOCUMENT
  geometry in front of it -- it draws after the OIT composite, over
  the blended result. Fixing that means joining WBOIT, rejected
  above.

The sim's runs after the move: scene run of thirteen --
`SimPassScene`, `SimPassBaseShape` (still real: standalone draws the
base itself), the AO effect's nine, `SimPassResolve`,
`SimPassComposite` -- and an overlay run of two. Fifteen passes, and
the `SimPass*` enum renumbers once.

### 10.5 Stock at mPathStep == -1: ruled, the engine does not take it

Section 8.10's design lead -- the engine owning the uncut stock until
the first cut -- is DECLINED (ruled 2026-08-27): no visual hand-back
is wanted, and the lead cannot exist without one. The simulator draws
the stock in every state, uncut included. With that, the ground 8.10
kept `Dummy3DViewer::stockViewProvider` on is gone again; its
disposition is an open question, and it is not to be deleted without
a ruling.

### 10.6 Work breakdown

Each step builds and leaves both flavours and the legacy GL path
working; legacy GL is untouched throughout (it never attaches,
section 9).

1. **The bind-info struct.** `bindFrame`'s parameters fold into one
   struct. Mechanical, no behaviour change.
2. **The phase contract.** `overlayPasses()`, the
   `ViewConsumerScene0..15` run, the overlay run shrunk to four, the
   declPass loops and the id mapping. *Done when:* a probe consumer
   (not the simulator, same order as 8.8) draws a scene-run triangle
   that the fog covers and transparent scene geometry blends over,
   and an overlay-run triangle that draws over the OIT composite.
3. **Composite depth in both flavours.** The standalone degeneracy of
   10.3. *Done when:* the standalone image is pixel-identical and the
   probe shows depth present behind it.
4. **The simulator adopts the scene run.** *Done when:* transparent
   document geometry blends over the carved stock and volumetrics
   land on it; the attached image is otherwise unchanged.
5. **The path moves to the overlay run.** `mRPathTarget` deleted, the
   two host-space passes in, the decode in `fs_camsim_line`. *Done
   when:* the x-ray is back, including through the engine-drawn base.
6. **Doc.** The execution record.

### 10.7 Risks, each with its substitute

- **A pass with no stated transform inherits a stale one** (the 8.8
  lesson). The two path passes state theirs every frame, host camera
  or sim camera.
- **Depth-domain seam on the visible path.** The stock's depth is
  reconstructed per composite texel from the position attachment; the
  path's is interpolated per fragment. Where the visible path lies
  exactly on the stock surface the two can z-fight into stipple.
  *Substitute if seen:* a slightly-closer projection on the path
  passes, the `SimPassBaseShape` trick.
- **The prepass-fed effects still do not see the stock.** Engine AO,
  shadows, outlines and ground reflection read the prepass, which the
  consumer is not in -- same as stage 2, unchanged; listed so nobody
  hunts it as a regression of this stage.
- **The overlay run is four wide.** A consumer wanting more re-opens
  the split's sizing, not the facade's shape.
- **Renumbering `SimPass*`.** The AO range rides `SimPassAOFirst`;
  one definition, no scattered constants.

### 10.8 Verification

The stage 2 harness, extended: standalone leg (regression,
pixel-identical), attached `SIM_BASE=3 SIM_BOTH=1` (the piercing bar
-- the new picture is the faint path through the bar and the base),
a transparent-pane scene (document glass in front of the stock must
BLEND over it, the section 8.2 limit lifted), and the `SIM_LEGACY=1`
leg, whose diff class now includes path colour as documented in 10.4.
Real GPU on WSLg d3d12 as before: the frame presents and the ordering
is right.

### 10.9 Execution record (2026-08-27, all five steps)

Landed as `b4f0021ab0` (step 1), `b68334fee3` (step 2, and with it
step 4), `e25f107dbc` (step 3), `a9055242dd` (step 5). What the plan
did not know in advance:

- **Step 4 collapsed into step 2.** Shrinking the overlay block to
  four made the old placement impossible for a fourteen-pass
  consumer, so the moment the runs split, the simulator HAD to take
  the scene run -- which is what `overlayPasses()`'s all-scene
  default does. There was nothing left for a separate step 4 commit
  to do.
- **Step 4's transparent-geometry evidence lives in the probe, not
  the harness.** The simulator's own window has no transparent
  document geometry to stage -- its viewer's scene graph holds
  exactly the stock and base mirrors -- so the 10.8 transparent-pane
  scene is not stageable there today; it becomes stageable when a
  consumer attaches to a document view (section 11.1). The probe
  carries the evidence instead: it hand-builds a scene feed (a
  `MeshData`/`DrawCall` made from static arrays -- the facade needs
  no Coin cache to be fed) with a half-transparent quad in front of a
  consumer scene-run triangle, and the quad's WBOIT blend lands OVER
  the triangle at the numerically exact linear 50/50
  (188/188/188-class pixels, the sRGB re-encode of 0.5). Under the
  old contract that pixel was pure triangle. The overlay-run triangle
  draws unblended over the result, and an unused scene pass stays
  declared-but-dead.
- **Fog was not separately probed.** Volumetric inscatter needs a
  light and medium configuration a bare probe does not carry; the
  placement mechanism it rides -- pass-id order -- is the same one
  the WBOIT evidence exercises, and `ViewConsumerScene0` sits before
  `ViewCaustics` and `ViewVolApply` by construction.
- **Two probe traps, for the next probe author.**
  `QOpenGLWidget::grabFramebuffer` reads back BLACK: it re-renders
  into a grab FBO the backend's external blit never reaches -- grab
  the screen (`QScreen::grabWindow`) and crop the widget's rect, the
  way the sim harness always has. And a standalone probe must set
  `Qt::AA_ShareOpenGLContexts` before `QApplication` (what
  `Gui::Application` does at startup) or the library context cannot
  blit into the widget's FBO at all -- the same all-black symptom
  from a different cause.
- **The effect tier's contiguity check narrowed** from
  whole-id-array to the effect's own pass range: the two runs are
  not contiguous with each other by construction, and only the range
  the effect draws in has to be.
- **The composite-depth degeneracy lives in the simulator, not the
  surface.** A standalone surface has no camera to answer
  `hostCamera()` with; `RenderCompositeFacade` supplies its own view
  and projection as the fallback, which is where the "one code path"
  of 10.3 actually sits.
- **The coplanar z-fight stipple (10.7) did not materialize** -- the
  visible path renders solid over the carved floor on llvmpipe and on
  the d3d12 real-GPU leg both. The substitute stays in 10.7 unused.

Verification came out as 10.8 promised, minus the unstageable pane
scene above: the attached and standalone piercing-bar legs each
differ from their stage 2 baselines in 1013 pixels, every one on the
path lines -- the exact-colour change of 10.4 plus the recovered
x-ray, a faint stripe crossing the bar in both flavours (attached:
the ENGINE-drawn bar -- the section 8.9 loss, recovered and widened
as ruled). The AO leg at the renumbered effect range is pure
darkening over the scene (the only brighter pixels are the toggled
toolbar button's own icon). The legacy GL leg is BIT-IDENTICAL to its
prior-session baseline -- the restoration's A/B stands untouched.

## 11. Requested: the cut shape in the NORMAL 3D view

Asked for 2026-08-27, not designed. Today the simulator's picture
lives only in its own MDI window; the normal 3D view shows the Job's
`Stock`, which is an ordinary document object
(`Path/Main/Stock.py`, `addObject("Part::FeaturePython", "Stock")`)
and is always the UNCUT shape. The request is to see the carved
result -- partially carved, mill in progress -- in the normal view.

Two routes, complementary rather than competing.

### 11.1 Route A -- the renderer route (pixels)

`Renderer::setFrameConsumer` is per-RENDERER, and every 3D view has
its own, so a consumer can attach to ANY view. Section 8 built that,
and step 4 gave the consumer a way to sort against the host's document
geometry by depth. Pointing `DlgCAMSimulator` at the document's main
3D view is therefore closer to a configuration change than to new
machinery.

What it costs:

- render cache must be in the renderer mode. Legacy GL cannot borrow
  a frame, so this feature has no fallback (section 9).
- The Job's `Stock` object would draw UNCUT over the carved one --
  the mirror problem of section 8.8, one level up. It has to be
  hidden while the simulator draws.
- One consumer per view.
- **Pixels only.** No geometry: nothing to select, snap to, measure,
  export or save, and a click on the carved surface hits whatever is
  behind it.

! The main 3D view has far more to sort against than the simulator's
window, transparent geometry included -- which is exactly what section
10 is meant to lift. Route A wants the stage 3 contract, not the
current one, so stage 3 comes first.

### 11.2 Route B -- the geometry route (a real shape)

`PathSimulator/App/VolSim.cpp`, the older CPU volumetric simulator,
already tessellates to a mesh (`Tessellate(meshOuter, meshInner)`).
That yields an actual object: selectable, measurable, exportable,
storable in the document.

Cost: a different simulator (dexel/voxel), much slower, and meshing
every frame for live scrubbing would hurt. Meshing ON DEMAND -- "the
shape at step N" -- is the feasible shape of it.

### 11.3 Ruled: both, swapped on run state

**Pixels while the simulator is running; a mesh once it stops** --
debounced, and computed off the GUI thread. Ruled 2026-08-27. Stage 3
comes first.

So Route A is the live view and Route B is what settles in its place
when the operator stops to look at something. Neither replaces the
other.

### 11.4 What already exists (checked, not assumed)

Both halves are further along than section 11.2 suggested.

- `PathSimulator/App/` is built **unconditionally** -- `CMakeLists.txt`
  gates only `AppGL` behind `BUILD_CAM_SIMULATOR_GL`. The volumetric
  simulator is in every build.
- It has a full Python API already (`PathSim.pyi`):
  `BeginSimulation(stock, resolution)`, `SetToolShape`,
  `ApplyCommand(placement, command)` and
  **`GetResultMesh() -> tuple[Mesh, Mesh]`** (outer, inner).

`ApplyCommand` being per-command is what makes "the shape at step N"
reachable at all. So the work is not building either half -- it is the
SWAP.

### 11.5 The open questions the swap raises

- **! Two simulators, two states.** `AppGL/MillSimulation` and
  `App/PathSim` are independent, and the mesh must land at the same
  point in the path as the pixels it replaces or the swap will jump.
  The volumetric side steps whole COMMANDS (`ApplyCommand`); the GL
  side tracks `mPathStep` plus a `mSubStep` within a segment. Those
  granularities may not meet, in which case the swap is only exact at
  command boundaries. Settle this before designing the debounce --
  it decides whether "stopped" can mean anywhere or only at a
  boundary. Proposed resolution (2026-08-27, not yet ruled):
  `ApplyCommand` takes a placement plus a command, and G-code motions
  interpolate, so a stop mid-segment can be met by SYNTHESIZING a
  truncated final command -- the same command with its endpoint at
  the GL sim's sub-step interpolant. The replay should also run from
  a SNAPSHOT of the stopped state on the worker, aborted if play
  resumes.
- **Threading.** The tessellation goes to a worker; anything touching
  the document or the Coin graph must come back to the GUI thread.
- **Where the mesh lives: RULED.** It goes on the Job's `Stock`
  object as a property, and its view object gains a display mode to
  choose the uncut or the cut shape. See 11.6.
- **Debounce hook.** `MillSimulationState::mSimPlaying` with
  `SetPlaying(b)` is the running/stopped signal;
  `MillSimulation.cpp:556` clears it at the end of a run.

### 11.6 The cut mesh lives on the Stock object (ruled 2026-08-27)

**A property on `Stock` persists the mesh; a display mode on its view
object chooses uncut or cut.** That keeps one object where there would
otherwise be two, and the mesh survives save and reload with the
document rather than being recomputed on open.

What the code looks like today, checked:

- `Stock` is a `Part::FeaturePython` (`Path/Main/Stock.py:396`), so its
  view provider is the Part one, and its display modes are Part's
  (Flat Lines, Shaded, Wireframe, Points). `SetupStockObject` leaves it
  on `Wireframe` at `Transparency = 90` -- deliberately unobtrusive,
  which a solid cut mesh will want to override in its own mode.
- Its Python view provider proxy is
  `Path.Base.Gui.IconViewProvider.ViewProvider`, which exists ONLY to
  supply an icon. Its `attach()` records `vobj`/`obj` and builds no
  scene graph, and it declares no display modes.
- `Mesh::PropertyMeshKernel` is a registered property type
  (`MeshProperties.cpp:46`, deriving `App::PropertyComplexGeoData`),
  so `addProperty` can create it and it persists.

The work, then:

1. Add the mesh property in `SetupStockObject`, alongside the existing
   `StockType`.
2. Give `Stock` its own view provider rather than the bare icon one --
   or extend that one -- so it can `addDisplayMode` a node for the cut
   mesh, list it in `getDisplayModes`, and rebuild the node from
   `updateData` when the property changes. On a `Part::FeaturePython`
   a Python proxy's display modes are ADDED to the Part ones, so the
   uncut modes stay as they are.
3. Keep `dumps`/`loads` in step with any new proxy state.

! The mesh is only pickable, and so only measurable, in the mode that
shows it. That is the point of putting it behind a display mode, but
it does mean "select the cut shape" and "select the stock" are the
same object in different modes, not two things.
### 11.7 The design (2026-08-28)

Written against the code, with the unruled calls marked PROPOSED.
The rulings this rests on: both routes swapped on run state (11.3),
the mesh on the Stock object behind a display mode (11.6), meshing
debounced and off the GUI thread (11.3), and stage 3's phased
contract, which is what lets the consumer's stock sort against a
document view's transparents (10.2).

#### 11.7.1 Route A mechanics -- what actually has to switch

`DlgCAMSimulator::attachToHost()` already accepts any
`View3DInventorViewer`; the target is the only thing hard-wired
(`ViewCAMSimulator::updateHostAttachment` passes `mDummyViewer`).
Attaching to a document view instead touches four things, all of them
"which viewer" plumbing rather than new rendering:

- **Camera.** The sim renders its G-buffer under its own camera and
  the composite reprojects only DEPTH into the host's (10.3) -- the
  colour is from the sim camera's viewpoint. Attached to the sim's
  own window the two are the same camera, so nothing shows; attached
  to a document view they are not, and the picture would be pasted
  from the wrong viewpoint. So `updateCamera()` must read the
  ATTACHED viewer's `SoCamera`, not `mDummyViewer`'s,
  whenever the host is foreign. Navigation then comes free: the
  document view owns its camera and the sim follows it.
- **Redraw routing.** `requestRedraw()` asks the dummy viewer's
  render manager; attached elsewhere it must ask the host view's.
- **Who draws the stock.** The Job's `Stock` object is real document
  geometry in that view and would draw UNCUT over the carve (11.1).
  Hide it (`Visibility = False`) on attach, restore on detach. This
  is the one place Route A touches the document.
- **Who draws the base.** In the sim's window the base is a mirror
  copy fed to the dummy viewer (8.4). In a document view the model
  IS already there as the real objects, lit and selectable;
  `SetBaseDrawnByHost(true)` stands the sim's copy down and nothing
  needs feeding at all. Occlusion against the model comes through
  the shared depth.

One host at a time: `SimDisplay`'s G-buffer and resolve targets are
single-size, recreated on `UpdateWindowScale` -- serving two
differently-sized hosts would recreate them every frame. The
consumer MOVES between hosts; it never draws in two.

Detach triggers, both of which the sim window never had to face: the
document view can CLOSE under the consumer (QObject::destroyed of
the MDIView -> detach, restore Stock visibility), and its renderer
can be torn down by a preference change (the existing
`updateHostAttachment` re-call answers that when it runs; the
attach-target bookkeeping moves into `DlgCAMSimulator` so both hosts
are handled by one path).

**PROPOSED UX (not ruled): a toggle button in `GuiDisplay`,**
next to the existing view buttons -- "show in the 3D view". The sim
window opens exactly as today and keeps every control; toggled on,
the consumer re-targets to the job document's active 3D view, and
the sim's own window falls back to its Coin mirrors (uncut stock +
base -- the dummy viewer has held those providers all along). Toggle
off, or close the sim, and everything restores. Alternative shape,
listed for the ruling: never open the sim MDI window at all and run
from the task panel alone -- rejected here because the playback
controls (pause, speed, single-step) live in `GuiDisplay` and would
all need a second home.

#### 11.7.2 The stop swap -- who drives it

The driver is PYTHON, in `SimulatorGL.py`. It already holds the
job, the checked ops, each op's tool (profile AND solid shape) and
the command lists it fed to the GL sim -- exactly the inputs
`Simulator.py` feeds VolSim. The C++ side does not know the
`Path::Command`s any more (they were flattened to `MillMotion`s at
`AddCommand`), so driving the replay from C++ would mean
reconstructing what Python still has.

What C++ must newly surface (the `CAMSim` binding):

- **A command-to-motion map.** `AddCommand` parses one command into
  0..n `MillMotion`s (`GCodeParser::Operations`); recording the
  motion count per command at that moment is the whole map.
- **Progress.** `GetProgress() -> (motionIndex, fraction, playing)`:
  `mPathStep` + `mSubStep / numSimSteps` from the last
  `CalcSegmentPositions`, and `mSimPlaying`. Python inverts the map
  to (commandIndex, motion-within-command, fraction).

**PROPOSED: polling, not a callback.** A QTimer in the Python driver
(~250 ms) polls `GetProgress()`; the debounce IS the poll -- N
consecutive quiet polls with `playing == false` and an unchanged
position (PROPOSED default: 2, i.e. ~0.5 s) starts the replay. A
C++-to-Python callback would need lifetime and GIL care for no
gain at this event rate.

#### 11.7.3 The replay -- snapshot, thread, truncated tail

On debounce expiry the driver snapshots `(commandIndex, fraction)`
and starts a worker `threading.Thread`:

    PathSim.BeginSimulation(stock, resolution)
    for each active op up to the snapshot:
        SetToolShape(op tool solid)
        ApplyCommand(...) each WHOLE consumed command, verbatim
    for the partially-consumed command:
        synthesize the tail (below)
    outer, inner = GetResultMesh()
    marshal to the GUI thread; drop the result if cancelled

- **GIL: the `PathSim` bindings must release it** around
  `BeginSimulation` / `SetToolShape` / `ApplyCommand` /
  `GetResultMesh` (`Py_BEGIN_ALLOW_THREADS`), or the "worker" blocks
  the GUI anyway. They are pure C++ on their own state; nothing they
  do touches Python.
- **Cancel**: a flag checked between `ApplyCommand` calls. Play
  resuming, a new stop superseding, or the panel closing sets it;
  the thread finishes its current command and exits. Results carry
  their snapshot and are dropped when it is no longer the current
  one.
- **The truncated tail (11.5's PROPOSAL, refined) -- and then the
  whole replay went the tail's way.** The GL sim's position is a
  MOTION index + fraction, so the tail was to be synthesized from
  motions while whole commands replayed verbatim. Building it
  showed the parser had already done the hard part for EVERY
  motion: sticky words resolved, drill cycles pre-expanded into
  moves (GCodeParser::AddLine splits G73/G81/G82/G83 into approach/
  plunge/retract), tool-change lines contributing no motion at all.
  So the BUILT replay runs entirely off the motion list -- lines as
  G1 to the (fraction-interpolated) endpoint, arcs as G1 chords
  over the fraction-scaled sweep (the raw i/j centre offsets ride
  on the motion), tool changes where the motion's tool number
  flips. Exact parity with what the GL sim executed, one code
  path, no boundary between "whole" and "partial". Rapids are fed
  too: the GL sim cuts on every move and VolSim treats G0 as G1 --
  Simulator.py's skipping of rapids is a fidelity bug this replay
  does not copy.
- **The two meshes are halves, not variants** (checked in
  `cStock::Tessellate`): `outer` holds the facets still on the
  ORIGINAL stock surface, `inner` the MACHINED facets -- each alone
  has holes, the shape is their union. So the landing merges them,
  outer first, and records the outer facet count in a second
  property; the Cut mode then colours the two ranges separately
  (per-face material binding), keeping the two-tone picture the GL
  sim draws (stockColor for virgin surface, cutColor for cuts) --
  the swap does not flatten the image to one colour.
- **Landing (GUI thread)**: set `Stock.CutMesh` (merged) and
  `CutMeshUncutCount`, switch the view object to the `Cut` display
  mode and show it, and stand the sim's stock draw down
  (`SetStockVisible(false)` on the sim side, NOT the document
  object). Tool and path overlay keep drawing -- PROPOSED, on the
  argument that the path over the settled mesh is exactly the x-ray
  feature (10.4). On play: reverse in order (sim stock back on,
  Stock object back to hidden-while-attached, mode restored) before
  the first new frame.

#### 11.7.4 The Stock object half (ruled in 11.6, concrete shape)

- `SetupStockObject` adds `Mesh::PropertyMeshKernel` `CutMesh`
  (group "Stock", `Prop_Output` so it never touches the recompute
  DAG) plus `CutMeshUncutCount` (integer: leading facets of CutMesh
  that are un-machined stock surface, -1 = unknown/single-colour),
  and `onDocumentRestored` back-fills both into old documents.
- A `StockViewProvider` in the stock's own Gui module extends
  `IconViewProvider.ViewProvider`: `attach()` builds an
  `SoSeparator` (`SoCoordinate3` + `SoNormal` + `SoIndexedFaceSet`)
  registered via `addDisplayMode(sep, "Cut")`; `getDisplayModes`
  lists it; `updateData` repopulates the coordinates from `CutMesh`
  when it changes (points/facet index arrays through the Mesh
  Python API's arrays, not per-vertex Python loops); `dumps`/`loads`
  keep the icon behaviour. Existing documents restore the OLD proxy
  class; `onDocumentRestored` swaps it. On a `Part::FeaturePython`
  the proxy's modes ADD to Part's, so Wireframe et al. survive.
- The mesh mode carries its own materials (the uncut stock's
  translucent wireframe look is wrong for a solid carve): the GL
  sim's stockColor for the un-machined facet range and its cutColor
  for the machined one, per-face-indexed off `CutMeshUncutCount`.
- **Replay detail from `Simulator.py` (checked)**: VolSim's
  `ApplyCommand` is only fed linear moves there -- arcs are expanded
  to G1 chains sized by the resolution, and drill cycles (G73/G81/
  G82/G83) to G0/G1 sequences, by the DRIVER. The swap's replay
  reuses that expansion for whole commands, and the motion-derived
  tail runs through the same helper.

#### 11.7.5 Work breakdown

1. **Stock half** (ruled, independent of every open UX call):
   property + view provider + `Cut` display mode + restore path.
   Verify headless-ish: assign a mesh from the console, switch
   modes, save, reload.
2. **Progress surface**: command-to-motion map in
   `CAMSim::AddCommand`, `GetProgress` binding, `mSimPlaying`
   exposure.
3. **GIL release** in the four `PathSim` bindings.
4. **Swap driver** in `SimulatorGL.py`: poll + debounce + worker
   replay + truncated tail + landing/reversal.
5. **Route A re-target**: camera source, redraw routing, stock/base
   disposition, the GuiDisplay toggle, detach-on-close/teardown.
6. **Doc + records.**

Steps 1-3 carry no unruled decision; 4 carries the polling and
tail proposals; 5 carries the UX proposal.

Open for ruling: the 11.7.1 UX shape; polling + debounce default
(11.7.2); tail synthesis from motions (11.7.3, refines 11.5's
proposal); path overlay staying up over the settled mesh (11.7.3);
the Cut mode's material (11.7.4). Still pending from stage 3:
BUILD_BGFX / BUILD_CAM_SIMULATOR_GL defaults, and the sim-window
stock view provider's disposition (10.5).

### 11.8 Steps 1-4 built and verified (2026-08-28)

Commits: b03c3c5543 (core enum fix), 34f3c92cef (Stock half),
ec57a5bb72 (progress surface), ff6131161d (GIL release),
acce852d23 (the swap driver). Route A -- step 5 -- is NOT built;
its UX shape is still open for ruling (11.7.1).

What building them settled:

- **The core had a real migration gap.** A proxy swapped in after
  the view provider attached (every pre-feature document, since the
  Gui restores proxies AFTER the App side's onDocumentRestored --
  which is why EnsureViewProvider is called lazily by the driver,
  not eagerly at restore) could register display mask modes but
  never get them into the DisplayMode enum, stated only in
  attach(). ViewProviderFeaturePython::onChanged(Proxy) now
  restates the enum on a late swap; the probe drives the swap BOTH
  ways (icon proxy back in, then EnsureViewProvider) and selects
  Cut afterwards.
- **CAMSim getters must not create.** DlgCAMSimulator::instance()
  builds the simulator MDI view on demand -- the first smoke run
  opened a window from a bare GetProgress(). The getters go through
  a new existingInstance() that returns null instead.
- **VolSim's two result meshes are halves, not variants** (11.7.4):
  outer = facets still on the original stock surface, inner =
  machined. Merged outer-first; CutMeshUncutCount records the
  split; the Cut mode colours the ranges per-face-indexed
  (SoMaterialBinding PER_FACE_INDEXED, two-entry material) in the
  GL sim's stockColor/cutColor.
- **Verification** (xvfb, real Job): stock1 probe -- property
  status Output + touch-clean landing (nothing in the recompute
  DAG moves), Cut in the enum, two-tone index ranges, migration,
  mesh + mode + rebuilt node across save/reload. swap4 probe --
  hand-fed G-code with an arc, stage-slider seek to 60%: debounced
  worker replay lands a 566-facet two-tone mesh at motion 3
  fraction 0.92 (a MID-ARC truncation); re-seek to 35% re-lands
  290 facets; screenshot shows the carved slot on the Stock in the
  NORMAL 3D view, blue virgin surface, green cut. Teardown abort
  at exit is the known WSLg one, after DONE.
- The landed mesh deliberately SURVIVES closing the simulator --
  the settled cut shape outliving the run is the point of Route B.
  A stale landed mesh from a previous run also stays visible while
  a NEW run's pixels play in the sim window; the first stop of the
  new run overwrites it. Revisit only if it confuses.

Harness: scratchpad swap4/probe.py (job + hand-fed sim + seek +
land/re-land asserts + screenshot), stock1/probe.py (the Stock
half alone).

### 11.9 Route A shape: multiple hosts, evaluated (2026-08-28)

Asked for instead of ruling on 11.7.1's single-host toggle: what
would it take for the sim to draw in MORE than one view at once --
its own window AND the document view -- and is it worth it. "Multiple
consumer" has two readings; they are independent:

- **One consumer, many hosts** (the sim registered on several
  renderers). This is the one the UX needs, and the evaluation
  below.
- **Many consumers, one renderer** (several plugins inside one
  view's frame). Orthogonal: a consumer list plus a pass-budget
  split per renderer. Nothing today needs it; not evaluated
  further.

#### What the survey found (all checked, not assumed)

- **The renderer API is ALREADY multi-host.** setFrameConsumer's
  state lives per-renderer (BGFXRenderer pimpl: frameConsumer +
  consumerSurface); registering the same consumer object on two
  renderers creates two surfaces, and each host frame hands its own
  surface to drawFrame. No API change, no contract change beyond
  wording.
- **The sim's stepping is TIME-based, not call-based.** SimNext
  converts elapsed wall time to steps, so two hosts calling
  drawFrame at independent cadences advance the animation by
  exactly wall time between them -- no double-advance, no
  advance/render split needed. mSingleStep consumes its one step on
  whichever host draws first; the other renders the new state.
  ProcessSim = SimNext + Render is already two calls.
- **What is actually per-host is SimDisplay's render state**: the
  G-buffer set (colour + position + normal + normalZ + depth), the
  resolve and AO textures/targets, mWidth/mHeight, and the camera
  matrices (mMatLookAt/mProjMat -- the CSG is VIEW-DEPENDENT, the
  carve is computed in screen space under the host camera). Roughly
  15 members become a per-host context struct keyed by the
  DrawSurface; MillSimulation (segments, tools, step state, clock)
  stays shared. DlgCAMSimulator's single mHostRenderer becomes a
  small list; updateCamera reads the host that is drawing;
  requestRedraw asks every attached host's render manager.

#### The two real costs

- **The CSG runs per host per frame.** The material removal IS the
  rendering, under each host's camera -- there is no shared carve
  to reuse. Today the G-buffer doubles as a per-camera cache
  (repaints without a step advance skip the CSG); per-host contexts
  keep that property per view, but a playing simulation pays the
  full stencil CSG once per attached view per frame. The document
  view is usually the BIGGER viewport, so while attached and
  playing, expect the sim's GPU cost to roughly track total
  attached pixels -- 2-3x the sim window alone. Parked views do not
  repaint and cost nothing (idle accumulation etc. notwithstanding).
- **G-buffer memory per host.** Colour RGBA8 + two RGB32F +
  normalZ + depth is on the order of 40-50 bytes per pixel: a 2MP
  document view adds ~80-100MB while the simulator is attached to
  it. Demand-allocated and freed on detach (the handle-pool
  discipline), but real. The alternative -- ONE max-size G-buffer
  shared sequentially within the frame (view-id blocks do not
  interleave, so host A's passes complete before host B's) -- was
  considered and REJECTED: it kills the per-camera cache (each
  host's repaint clobbers the other's cached carve, so every
  repaint of either view re-runs the CSG for both) and buys only
  memory that detach already reclaims.

#### What it buys

- The operator keeps the dedicated sim window -- controls, isolated
  navigation, guaranteed framing -- AND sees the carve in context in
  the document view, sorted against the real model, transparents,
  section planes, the works (stage 3's contract). No modal toggle,
  no "which view has the picture" state to explain, no mirror
  fallback in the sim window.
- The per-host context refactor is the same shape a consumer needs
  to appear in SPLIT VIEWS of one document (docs/SplitViews.md) --
  single-host code can never serve those.
- Route B is unchanged and still owns the stopped state.

#### What stays true regardless

- Doc-view pixels remain pixels: nothing selectable, clicks fall
  through (11.1). The Job's Stock still has to hide while attached,
  and the sim must not draw its base copy there (the model is real
  geometry in that view). Per-host who-draws-what booleans already
  exist (SetBaseDrawnByHost); they become per-host config.
- Legacy GL is single-host by nature (it owns its widget) and is
  untouched.

#### Assessment

The blocker named in 11.7.1 -- "one host at a time, single-size
G-buffer" -- turned out to be the sim's bookkeeping, not the
architecture: the API is already multi-host and the clock already
composes. The honest cost is the per-host CSG run (GPU, while
playing, proportional to attached pixels) and ~100MB per attached
2MP view, both bounded by "detach when not wanted". The work is a
mechanical-but-wide SimDisplay refactor (per-host context struct,
~15 members, every method that touches targets/camera/size picks a
context) plus small DlgCAMSimulator list-keeping -- comfortably
smaller than stage 3, larger than the 11.7.1 toggle. Staging that
keeps a working simulator at every step:

1. Per-host context refactor with exactly ONE host -- pure
   mechanical move, verified bit-identical against the stage 3
   baselines (s3att/s3std/gl1).
2. Host list + per-surface context lookup + redraw fan-out; the
   sim window and a second attach verified side by side.
3. The document-view attach itself: Stock hiding, base
   disposition, camera source, detach on view close -- 11.7.1's
   plumbing, now additive instead of a toggle.

RECOMMENDED if the both-views UX is wanted (and it reads like it
is): the refactor is the right long-term shape (split views want it
too), the runtime costs are opt-in by attachment, and the staging
keeps every intermediate state shippable. The 11.7.1 toggle remains
the cheap fallback if priorities shift.

### 11.10 Multi-host built, all three stages (2026-08-28)

Four commits: e5e1e40986 (stage 1), 8451e63597 + 1d1784b168
(stage 2), 7370cc2540 (stage 3). The 11.9 staging held; what follows
is what the build added to it.

#### Stage 1 -- the per-host context

`SimHostContext` (SimDisplay.h) holds everything per-host: size, the
camera dedup state and the matrices built from it, the facade
G-buffer and resolve target, the AO effect with its cached result,
and the recalculate flag. The public `updateDisplay` bool became an
API -- `InvalidateDisplay()` (the SIMULATION changed: every host
stale) vs `NeedsRecalculate()`/`ClearRecalculate()` (the current
host's flag) -- because with more than one host the two meanings the
bool conflated really are different operations; the camera and size
paths set only their own host's flag. Legacy GL's FBOs and the
shared programs/uniforms/quad stay on SimDisplay.

Verified bit-identical the strong way: the committed d5 baselines
turned out to carry an unknown env (the diffs against them were
identical across leg pairs -- a scene difference, not a renderer
one), so the pre-change tree was rebuilt from stash and re-run under
the same flags. All four legs (attached / standalone / legacy GL /
SSAO, SIM_BASE=3) diff 0 of 1260000 pixels against it.

#### Stage 2 -- the host list

`SetCurrentHost(surface)` selects-or-creates the context at the top
of every frame (null = the default context, which serves legacy GL);
`DropHost(surface)` releases one before its surface dies. The AO
effect moved from InitShaders to context creation -- a per-surface
context can appear at any time after init. One new engine API:
`Renderer::frameConsumerSurface()`, the registered consumer's
surface -- stable for the registration's lifetime, which makes it
the context key, and recoverable at detach time.

DlgCAMSimulator keeps the primary attachment (the sim window's
dummy viewer, which still decides who owns the widget's picture) and
grows `attachExtraHost`/`detachExtraHost` plus redraw fan-out to
every host's render manager. The clock composed without any work,
as 11.9 predicted: SimNext converts elapsed wall time to steps, so
hosts drawing at independent cadences advance the one simulation by
wall time. MillSimulation's own window-scale dedup was DELETED --
per-host now, and SimDisplay's per-context check is the real one.
`ViewCAMSimulator::attachDocumentView` wires the document's first
View3DInventor as an extra host; Python reaches it through
`PathSim.AttachDocumentView`/`DetachDocumentView`.

#### Stage 3 -- the document-view attach for real

- **Camera source**: updateCamera(surface) resolves the DRAWING
  host's camera -- the extra host's own view camera, the dummy
  viewer's otherwise. The projection near/far stay synthesized from
  the stock size; identical fov/height and aspect mean the image
  aligns, and the composite's depth transform uses the host's real
  projection as before.
- **Lifecycle**: the attachment records the consumer surface at
  attach time (the cleanup paths need the KEY after the renderer is
  freed -- DropHost never dereferences it); a refused registration
  (pass budget) is a refused attach; the view's destroyed signal
  drops our side only (by then the renderer and surface are already
  gone); and requestRedraw self-heals when a render-cache change
  silently replaces a view's renderer.
- **Base disposition**: resolved to NO change. SetBaseDrawnByHost
  stays global because it is true for every host that exists: the
  sim window's mirror provider draws the base there, and in the
  document view the base IS the document's own geometry.
- **The driver**: _CutMeshSwap attaches while the pixels move --
  playing or scrubbing -- hiding the Stock in the document view
  (its uncut wireframe would sit on top of the carve), and
  detaches as the first act of landing, so the document view goes
  back to its own Stock in the Cut mode the landing selects.
  Pref-gated: Mod/CAM `SimulatorShowInDocumentView`, default true.

#### Verification record

- The four A/B legs stayed 0-diff through every stage (m2, m3 runs
  vs the regenerated stage-1 baselines).
- Side-by-side probe (scratchpad sim_multi.py): the carve top-down
  in the sim window and the stock front-on in the document view AT
  ONCE -- two cameras, one simulation, per-host CSG. The document
  view needs its camera placed explicitly in the probe (an empty
  document gives view-fit nothing to frame).
- End-to-end Job probe (swap5): seek -> "doc view ATTACHED (stock
  visible=False)"; settle -> land 566 facets, "docAttached=False
  stockVisible=True", mode Cut; reseek 35% -> the same cycle again,
  re-landing 290 facets.
- Closing the document view under a live attachment: the sim window
  keeps drawing, no crash before teardown (the exit-time abort in
  every probe log is the known pre-existing WSLg teardown crash).

Still open, unchanged by this work: the stock view provider's
disposition (10.5), and the poll defaults built as proposed
(11.7.5). The per-host shape is the one split views will reuse
(docs/SplitViews.md).

### 11.11 The attach toggle in the simulator overlay (2026-08-28)

Section 11.10 left the document-view attach reachable only through
`Mod/CAM SimulatorShowInDocumentView`, and the driver read that
preference ONCE, into `_CutMeshSwap.showInDocView`, when the
simulation panel opened -- so a change applied to the next session,
not this one. This section closes both: the operator can see and
drop the second host's GPU cost mid-session from the simulator's own
overlay.

#### The button

`docViewButton`, a checkable auto-raise `QToolButton` at the end of
the overlay row in `PathSimulator/AppGL/GuiDisplay.ui`, beside
`pathButton` and `ssaoButton` whose pattern it follows. It carries an
`objectName` so a probe can find it, and the icon is the core
`:/icons/window-new.svg` -- there is no spare `gl_simulator` PNG, and
"the drawing also goes to another window" is what the metaphor has to
say. `GuiDisplay` exposes `setDocViewEnabled(bool)` (signal-blocked,
like the other setters) and emits `docViewEnableChanged(bool)`.

#### One source of truth: the preference

The attach itself is driven from Python (`_CutMeshSwap`, which owns
the Stock visibility save/restore and the attach/detach timing), so
the C++ button must NOT call `AttachDocumentView` directly -- the
driver would re-attach on the next movement and the two would fight.
Instead:

- the button WRITES `Mod/CAM SimulatorShowInDocumentView`
  (`DlgCAMSimulator::connectTo`, a lambda on the signal);
- `CAMSettings` -- already a `ParameterGrp::ObserverType` on that
  group -- gained a `SimulatorShowInDocumentView` branch that pushes
  the value back onto the button through
  `DlgCAMSimulator::setDocViewEnabled`. That is also the
  initialisation: `applySettings()` fires the same branch, and it
  runs after `connectTo`, so `mGui` is there;
- `_CutMeshSwap` re-reads the preference every poll
  (`_showInDocView()`), and `_poll` detaches at once when it has gone
  false rather than waiting for the next mesh landing.

So the button, the preference and any future preferences-page
checkbox all move the same value, in either direction, and the 250 ms
poll is what "at once" means. The write-observe-setChecked round trip
does not loop: the setter blocks the button's signal.

#### Wish, not report

The button stays checked when the attach is REFUSED (the document
view has no renderer to borrow -- render cache outside mode 3, a
backend that would not start, or the pass budget full). The checked
state means "do this where it is possible", the tooltip says so, and
the attach happens by itself once the view can host it, because the
driver retries on every movement. Reporting the real state instead
would need the refusal to travel back from Python to the button and
would flicker with each renderer swap.
