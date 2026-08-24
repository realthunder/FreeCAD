# CAM simulator render port

Porting the CAM simulator's rendering off raw OpenGL. Survey and
decisions are settled; this is the plan of record.

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
  small.
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
