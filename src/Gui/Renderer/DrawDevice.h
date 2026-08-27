/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef RENDERER_DRAWDEVICE_H
#define RENDERER_DRAWDEVICE_H

/// \file DrawDevice.h
/// The immediate-mode draw facade (docs/CAMSimRenderPort.md section 3):
/// a small backend-free drawing API exported by FreeCADRenderer for
/// consumers that draw their own passes -- the CAM simulator first --
/// without linking the graphics backend. On Windows bgfx is linked
/// STATIC into this library, so a second linker of bgfx would get its
/// own copy of bgfx's global state; every outside consumer must come
/// through here instead.
///
/// This header owns the device (process-wide resource creation); the
/// per-widget frame surface is DrawSurface.h. No backend types appear
/// in either: handles are opaque 16-bit ids, sized so that the bgfx
/// implementation is a cast.

#include <FCConfig.h>

#ifdef FreeCADRenderer_STATIC
#   define RendererExport
#elif defined(FreeCADRenderer_EXPORTS)
#   define RendererExport   FREECAD_DECL_EXPORT
#else
#   define RendererExport   FREECAD_DECL_IMPORT
#endif

#include <cstdint>
#include <memory>

class QOpenGLWidget;

namespace Render {

class DrawSurface;

/// The invalid id every handle type defaults to.
static constexpr uint16_t InvalidDrawId = 0xffff;

struct VertexBufferHandle {
    uint16_t idx = InvalidDrawId;
    bool valid() const { return idx != InvalidDrawId; }
};
struct IndexBufferHandle {
    uint16_t idx = InvalidDrawId;
    bool valid() const { return idx != InvalidDrawId; }
};
struct ProgramHandle {
    uint16_t idx = InvalidDrawId;
    bool valid() const { return idx != InvalidDrawId; }
};
struct UniformHandle {
    uint16_t idx = InvalidDrawId;
    bool valid() const { return idx != InvalidDrawId; }
};
struct TextureHandle {
    uint16_t idx = InvalidDrawId;
    bool valid() const { return idx != InvalidDrawId; }
};
/// A framebuffer: one or more colour textures plus an optional
/// depth/stencil texture.
struct TargetHandle {
    uint16_t idx = InvalidDrawId;
    bool valid() const { return idx != InvalidDrawId; }
};
/// An instance of a built-in screen-space effect (the effect tier).
struct EffectHandle {
    uint16_t idx = InvalidDrawId;
    bool valid() const { return idx != InvalidDrawId; }
};

enum class DrawAttrib : uint8_t {
    Position,
    Normal,
    Color0,
    TexCoord0,
};

/// Component types a vertex attribute can carry. There is deliberately
/// no 32-bit integer: the backend has none, so an integer field (the
/// tool path's SegmentId) travels as Float -- exact below 2^24.
enum class DrawAttribType : uint8_t {
    Uint8,
    Int16,
    Half,
    Float,
};

/// Description of one vertex buffer's layout, declared attribute by
/// attribute in memory order.
struct VertexLayout {
    struct Element {
        DrawAttrib attrib;
        uint8_t num;            ///< components, 1..4
        DrawAttribType type;
        bool normalized;
    };
    static constexpr int MaxElements = 8;
    Element elements[MaxElements];
    int count = 0;

    VertexLayout &add(DrawAttrib attrib, uint8_t num, DrawAttribType type,
                      bool normalized = false)
    {
        if (count < MaxElements)
            elements[count++] = {attrib, num, type, normalized};
        return *this;
    }
};

/// Uniform types. Vec3 does not exist on purpose -- backends pad, so a
/// GL vec3 uniform is repacked to Vec4 when its shader is ported.
enum class UniformType : uint8_t {
    Sampler,
    Vec4,
    Mat3,
    Mat4,
};

/// Texture formats -- sized to what the facade's consumers use, not to
/// the backend's table. Three-channel float formats do not exist here
/// (no backend support): an RGB32F attachment becomes RGBA32F.
enum class DrawTextureFormat : uint8_t {
    R8,
    RGBA8,
    RGBA32F,
    D24S8,
};

/// Sampling flags for created textures. Default (0) is linear
/// filtering with repeat wrap.
enum DrawTextureFlags : uint32_t {
    TextureNone = 0,
    TexturePoint = 1u << 0,     ///< nearest filtering
    TextureClamp = 1u << 1,     ///< clamp U and V
};

typedef uint16_t ClearFlags;
enum : uint16_t {
    ClearNone = 0,
    ClearColor = 1u << 0,
    ClearDepth = 1u << 1,
    ClearStencil = 1u << 2,
};

/// Comparison function, for both the depth test and the stencil test.
enum class CompareFunc : uint8_t {
    Never,
    Less,
    LEqual,
    Equal,
    GEqual,
    Greater,
    NotEqual,
    Always,
};

/// Face culling, under the counter-clockwise-front convention.
enum class CullMode : uint8_t {
    None,
    Back,
    Front,
};

enum class BlendMode : uint8_t {
    None,
    Alpha,      ///< src-alpha / one-minus-src-alpha
};

enum class PrimitiveType : uint8_t {
    Triangles,
    Lines,
    LineStrip,
    Points,
};

/// The render state of one draw. The backend keeps no current state --
/// state is an argument of every submit -- so a GL consumer's
/// glEnable/glDepthFunc/glColorMask sequences become writes into one of
/// these, folded in at each draw site.
struct DrawState {
    bool colorWrite = true;
    bool alphaWrite = true;
    bool depthWrite = true;
    CompareFunc depthFunc = CompareFunc::Less;
    CullMode cull = CullMode::None;
    BlendMode blend = BlendMode::None;
    PrimitiveType primitive = PrimitiveType::Triangles;
    /// Substitute for glPolygonOffset, which has no backend equivalent:
    /// a constant depth bias the implementation applies to the
    /// projection. Positive pushes the draw away from the camera,
    /// negative pulls it closer; 0 is off.
    float depthBias = 0.0f;
};

enum class StencilOp : uint8_t {
    Keep,
    Zero,
    Replace,
    IncrSat,
    DecrSat,
    IncrWrap,
    DecrWrap,
    Invert,
};

/// The stencil state of one draw; disabled by default. Front and back
/// faces share it, which is all the consumers need so far.
struct StencilState {
    bool enabled = false;
    CompareFunc func = CompareFunc::Always;
    uint8_t ref = 0;
    uint8_t readMask = 0xff;
    StencilOp stencilFail = StencilOp::Keep;
    StencilOp depthFail = StencilOp::Keep;
    StencilOp depthPass = StencilOp::Keep;
};

/// The effect tier: engine-internal screen-space effects offered as
/// services. The consumer supplies the input the effect declares and
/// gets a result texture back, never seeing the passes in between.
enum class EffectType : uint8_t {
    /// Ambient occlusion. Input: the engine's prepass packing of
    /// normal + linear depth plus projection params; output: an R8
    /// occlusion texture.
    AO,
};

/// Passes an AO effect run may draw in: up to 6 depth-pyramid levels,
/// the gen pass and two denoise passes. The consumer reserves this
/// many pass ids from its surface for runEffect's firstPass range.
constexpr unsigned kAOEffectPasses = 9;

/// Tuning for one effect run. Zero-initialized means "the effect's
/// defaults"; fields grow with the effects (step 7 of the port adds
/// the AO set).
struct EffectParams {
    /// AO: -1 = the effect's default (GTAO when its programs loaded),
    /// else the engine's method index (0 classic kernel / 1 GTAO).
    int method = -1;
    /// AO: sample radius in the consumer's view units; 0 = a small
    /// generic default -- pass a scene-derived value (the engine uses
    /// 5% of the scene bounding-sphere size) for a comparable look.
    float radius = 0.0f;
    /// AO: occlusion darkening strength; 0 = the engine default (1).
    float intensity = 0.0f;
    /// The projection the effect input was rendered with (16 floats,
    /// column-major). Required for AO -- the gen pass reconstructs
    /// view-space positions from the input's linear depth with it.
    const float *proj = nullptr;
};

/// Process-wide resource ownership of the draw facade. One per
/// process, owned by the active backend; every created resource lives
/// until destroyed or the backend shuts down.
///
/// All calls must come from the GUI thread (the thread the backend
/// renders on). Data passed to the create functions is copied before
/// return, so the caller may free it immediately.
class RendererExport DrawDevice {
public:
    virtual ~DrawDevice();

    /// The active backend's device, or null while no backend that
    /// implements the facade has its graphics device up. Null is a
    /// state, not an error: the consumer keeps its fallback path (or
    /// stays blank) and asks again later -- the first 3D view or an
    /// explicit RendererFactory::warmup brings the device up.
    static DrawDevice *instance();

    /// True while the backend device is up. instance() only returns a
    /// device in that state, so this mostly answers "still?".
    virtual bool available() const = 0;

    /// The clip-space depth convention of this device: true when the
    /// near plane maps to NDC z = -1 (OpenGL), false when it maps to
    /// z = 0 (D3D, Vulkan, Metal). A consumer that writes gl_FragDepth
    /// has to map its own clip z into window depth itself, and that
    /// mapping is the only place the two differ -- pass this in as a
    /// uniform rather than testing the shader language, so the answer
    /// comes from the device that will actually run the draw.
    virtual bool homogeneousDepth() const = 0;

    virtual VertexBufferHandle createVertexBuffer(
            const void *data, uint32_t bytes, const VertexLayout &layout) = 0;
    /// \a int32: 32-bit indices instead of 16.
    virtual IndexBufferHandle createIndexBuffer(
            const void *data, uint32_t bytes, bool int32 = false) = 0;
    /// Load a shader pair from the compiled shader pack by base name
    /// (the .sc source's name, no path, no extension). The consumer's
    /// shaders compile into the same pack as the engine's own
    /// (fc_bgfx_compile_shaders takes the source directory).
    virtual ProgramHandle createProgram(const char *vsName,
                                        const char *fsName) = 0;
    virtual UniformHandle createUniform(const char *name, UniformType type,
                                        uint16_t num = 1) = 0;
    /// \a data may be null for an uninitialized texture; when given it
    /// must hold w*h texels of \a format.
    virtual TextureHandle createTexture2D(int width, int height,
                                          DrawTextureFormat format,
                                          uint32_t flags,
                                          const void *data = nullptr) = 0;
    /// A texture usable as a render-target attachment (and sampled
    /// afterwards).
    virtual TextureHandle createRenderTexture(int width, int height,
                                              DrawTextureFormat format,
                                              uint32_t flags = 0) = 0;
    /// A framebuffer from previously created render textures. The
    /// target references the textures, it does not own them.
    /// \a depthStencil may be invalid for a colour-only target.
    virtual TargetHandle createTarget(const TextureHandle *colors,
                                      int numColors,
                                      TextureHandle depthStencil) = 0;

    virtual void destroy(VertexBufferHandle) = 0;
    virtual void destroy(IndexBufferHandle) = 0;
    virtual void destroy(ProgramHandle) = 0;
    virtual void destroy(UniformHandle) = 0;
    virtual void destroy(TextureHandle) = 0;
    virtual void destroy(TargetHandle) = 0;

    /// Instantiate a built-in effect. Runs through
    /// DrawSurface::runEffect, which owns the pass ids the effect's
    /// internal passes draw in.
    ///
    /// The AO effect's input texture is the engine's prepass packing:
    /// octahedral-encoded viewer-facing view-space normal in .xy,
    /// positive linear view depth in .z, .w = 1 for written fragments
    /// (the target must clear to 0 so the background reads .w = 0).
    /// Rendered by the consumer into an RGBA32F attachment of its own
    /// target; the result comes back as an R8 occlusion texture
    /// (1 = open).
    virtual EffectHandle createEffect(EffectType type) = 0;
    virtual void destroy(EffectHandle) = 0;

    /// One frame surface per QOpenGLWidget; DrawSurface::create is the
    /// public door to this.
    virtual std::unique_ptr<DrawSurface> createSurface(
            QOpenGLWidget *widget, unsigned numPasses) = 0;
};

} // namespace Render

#endif // RENDERER_DRAWDEVICE_H
