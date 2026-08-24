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

#include <cstdio>

#include "BGFXRendererP.h"
#include "DrawDevice.h"
#include "DrawSurface.h"

// The bgfx implementation of the draw facade (DrawDevice.h /
// DrawSurface.h; docs/CAMSimRenderPort.md section 3). Facade handles
// are opaque 16-bit ids matching bgfx's own, so resource creation is a
// cast plus an enum translation. Both `using namespace Render` and
// `using namespace bgfx` are open here (BGFXRendererP.h), and the two
// vocabularies share type names on purpose -- everything below is
// written fully qualified.

namespace {

bgfx::TextureFormat::Enum toBgfxFormat(Render::DrawTextureFormat format)
{
    switch (format) {
    case Render::DrawTextureFormat::R8:
        return bgfx::TextureFormat::R8;
    case Render::DrawTextureFormat::RGBA8:
        return bgfx::TextureFormat::RGBA8;
    case Render::DrawTextureFormat::RGBA32F:
        return bgfx::TextureFormat::RGBA32F;
    case Render::DrawTextureFormat::D24S8:
        return bgfx::TextureFormat::D24S8;
    }
    return bgfx::TextureFormat::RGBA8;
}

uint32_t bytesPerTexel(Render::DrawTextureFormat format)
{
    switch (format) {
    case Render::DrawTextureFormat::R8:
        return 1;
    case Render::DrawTextureFormat::RGBA8:
        return 4;
    case Render::DrawTextureFormat::RGBA32F:
        return 16;
    case Render::DrawTextureFormat::D24S8:
        return 4;
    }
    return 4;
}

uint64_t toBgfxSamplerFlags(uint32_t flags)
{
    uint64_t res = BGFX_SAMPLER_NONE;
    if (flags & Render::TexturePoint)
        res |= BGFX_SAMPLER_POINT;
    if (flags & Render::TextureClamp)
        res |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    return res;
}

bgfx::Attrib::Enum toBgfxAttrib(Render::DrawAttrib attrib)
{
    switch (attrib) {
    case Render::DrawAttrib::Position:
        return bgfx::Attrib::Position;
    case Render::DrawAttrib::Normal:
        return bgfx::Attrib::Normal;
    case Render::DrawAttrib::Color0:
        return bgfx::Attrib::Color0;
    case Render::DrawAttrib::TexCoord0:
        return bgfx::Attrib::TexCoord0;
    }
    return bgfx::Attrib::Position;
}

bgfx::AttribType::Enum toBgfxAttribType(Render::DrawAttribType type)
{
    switch (type) {
    case Render::DrawAttribType::Uint8:
        return bgfx::AttribType::Uint8;
    case Render::DrawAttribType::Int16:
        return bgfx::AttribType::Int16;
    case Render::DrawAttribType::Half:
        return bgfx::AttribType::Half;
    case Render::DrawAttribType::Float:
        return bgfx::AttribType::Float;
    }
    return bgfx::AttribType::Float;
}

bgfx::VertexLayout toBgfxLayout(const Render::VertexLayout &layout)
{
    bgfx::VertexLayout res;
    res.begin();
    for (int i = 0; i < layout.count; ++i) {
        const auto &e = layout.elements[i];
        res.add(toBgfxAttrib(e.attrib), e.num, toBgfxAttribType(e.type),
                e.normalized);
    }
    res.end();
    return res;
}

bgfx::UniformType::Enum toBgfxUniformType(Render::UniformType type)
{
    switch (type) {
    case Render::UniformType::Sampler:
        return bgfx::UniformType::Sampler;
    case Render::UniformType::Vec4:
        return bgfx::UniformType::Vec4;
    case Render::UniformType::Mat3:
        return bgfx::UniformType::Mat3;
    case Render::UniformType::Mat4:
        return bgfx::UniformType::Mat4;
    }
    return bgfx::UniformType::Vec4;
}

uint64_t toBgfxDepthTest(Render::CompareFunc func)
{
    switch (func) {
    case Render::CompareFunc::Never:
        return BGFX_STATE_DEPTH_TEST_NEVER;
    case Render::CompareFunc::Less:
        return BGFX_STATE_DEPTH_TEST_LESS;
    case Render::CompareFunc::LEqual:
        return BGFX_STATE_DEPTH_TEST_LEQUAL;
    case Render::CompareFunc::Equal:
        return BGFX_STATE_DEPTH_TEST_EQUAL;
    case Render::CompareFunc::GEqual:
        return BGFX_STATE_DEPTH_TEST_GEQUAL;
    case Render::CompareFunc::Greater:
        return BGFX_STATE_DEPTH_TEST_GREATER;
    case Render::CompareFunc::NotEqual:
        return BGFX_STATE_DEPTH_TEST_NOTEQUAL;
    case Render::CompareFunc::Always:
        return BGFX_STATE_DEPTH_TEST_ALWAYS;
    }
    return BGFX_STATE_DEPTH_TEST_ALWAYS;
}

uint64_t toBgfxState(const Render::DrawState &state)
{
    uint64_t res = 0;
    if (state.colorWrite)
        res |= BGFX_STATE_WRITE_RGB;
    if (state.alphaWrite)
        res |= BGFX_STATE_WRITE_A;
    if (state.depthWrite)
        res |= BGFX_STATE_WRITE_Z;
    res |= toBgfxDepthTest(state.depthFunc);
    // The facade convention is counter-clockwise front faces, so
    // culling back faces culls the clockwise ones (same mapping the
    // engine uses for its ccw materials).
    switch (state.cull) {
    case Render::CullMode::None:
        break;
    case Render::CullMode::Back:
        res |= BGFX_STATE_CULL_CW;
        break;
    case Render::CullMode::Front:
        res |= BGFX_STATE_CULL_CCW;
        break;
    }
    if (state.blend == Render::BlendMode::Alpha)
        res |= BGFX_STATE_BLEND_ALPHA;
    switch (state.primitive) {
    case Render::PrimitiveType::Triangles:
        break;              // the default primitive, no PT bits
    case Render::PrimitiveType::Lines:
        res |= BGFX_STATE_PT_LINES;
        break;
    case Render::PrimitiveType::LineStrip:
        res |= BGFX_STATE_PT_LINESTRIP;
        break;
    case Render::PrimitiveType::Points:
        res |= BGFX_STATE_PT_POINTS;
        break;
    }
    return res;
}

uint32_t toBgfxStencilTest(Render::CompareFunc func)
{
    switch (func) {
    case Render::CompareFunc::Never:
        return BGFX_STENCIL_TEST_NEVER;
    case Render::CompareFunc::Less:
        return BGFX_STENCIL_TEST_LESS;
    case Render::CompareFunc::LEqual:
        return BGFX_STENCIL_TEST_LEQUAL;
    case Render::CompareFunc::Equal:
        return BGFX_STENCIL_TEST_EQUAL;
    case Render::CompareFunc::GEqual:
        return BGFX_STENCIL_TEST_GEQUAL;
    case Render::CompareFunc::Greater:
        return BGFX_STENCIL_TEST_GREATER;
    case Render::CompareFunc::NotEqual:
        return BGFX_STENCIL_TEST_NOTEQUAL;
    case Render::CompareFunc::Always:
        return BGFX_STENCIL_TEST_ALWAYS;
    }
    return BGFX_STENCIL_TEST_ALWAYS;
}

uint32_t toBgfxStencilOp(Render::StencilOp op, int slot)
{
    // The three op slots (stencil fail / depth fail / depth pass)
    // carry distinct shifts; slot selects which.
    switch (slot) {
    case 0:
        switch (op) {
        case Render::StencilOp::Keep: return BGFX_STENCIL_OP_FAIL_S_KEEP;
        case Render::StencilOp::Zero: return BGFX_STENCIL_OP_FAIL_S_ZERO;
        case Render::StencilOp::Replace: return BGFX_STENCIL_OP_FAIL_S_REPLACE;
        case Render::StencilOp::IncrSat: return BGFX_STENCIL_OP_FAIL_S_INCRSAT;
        case Render::StencilOp::DecrSat: return BGFX_STENCIL_OP_FAIL_S_DECRSAT;
        case Render::StencilOp::IncrWrap: return BGFX_STENCIL_OP_FAIL_S_INCR;
        case Render::StencilOp::DecrWrap: return BGFX_STENCIL_OP_FAIL_S_DECR;
        case Render::StencilOp::Invert: return BGFX_STENCIL_OP_FAIL_S_INVERT;
        }
        break;
    case 1:
        switch (op) {
        case Render::StencilOp::Keep: return BGFX_STENCIL_OP_FAIL_Z_KEEP;
        case Render::StencilOp::Zero: return BGFX_STENCIL_OP_FAIL_Z_ZERO;
        case Render::StencilOp::Replace: return BGFX_STENCIL_OP_FAIL_Z_REPLACE;
        case Render::StencilOp::IncrSat: return BGFX_STENCIL_OP_FAIL_Z_INCRSAT;
        case Render::StencilOp::DecrSat: return BGFX_STENCIL_OP_FAIL_Z_DECRSAT;
        case Render::StencilOp::IncrWrap: return BGFX_STENCIL_OP_FAIL_Z_INCR;
        case Render::StencilOp::DecrWrap: return BGFX_STENCIL_OP_FAIL_Z_DECR;
        case Render::StencilOp::Invert: return BGFX_STENCIL_OP_FAIL_Z_INVERT;
        }
        break;
    default:
        switch (op) {
        case Render::StencilOp::Keep: return BGFX_STENCIL_OP_PASS_Z_KEEP;
        case Render::StencilOp::Zero: return BGFX_STENCIL_OP_PASS_Z_ZERO;
        case Render::StencilOp::Replace: return BGFX_STENCIL_OP_PASS_Z_REPLACE;
        case Render::StencilOp::IncrSat: return BGFX_STENCIL_OP_PASS_Z_INCRSAT;
        case Render::StencilOp::DecrSat: return BGFX_STENCIL_OP_PASS_Z_DECRSAT;
        case Render::StencilOp::IncrWrap: return BGFX_STENCIL_OP_PASS_Z_INCR;
        case Render::StencilOp::DecrWrap: return BGFX_STENCIL_OP_PASS_Z_DECR;
        case Render::StencilOp::Invert: return BGFX_STENCIL_OP_PASS_Z_INVERT;
        }
        break;
    }
    return 0;
}

uint32_t toBgfxStencil(const Render::StencilState &stencil)
{
    if (!stencil.enabled)
        return BGFX_STENCIL_NONE;
    return toBgfxStencilTest(stencil.func)
        | BGFX_STENCIL_FUNC_REF(stencil.ref)
        | BGFX_STENCIL_FUNC_RMASK(stencil.readMask)
        | toBgfxStencilOp(stencil.stencilFail, 0)
        | toBgfxStencilOp(stencil.depthFail, 1)
        | toBgfxStencilOp(stencil.depthPass, 2);
}

uint16_t toBgfxClearFlags(Render::ClearFlags flags)
{
    uint16_t res = BGFX_CLEAR_NONE;
    if (flags & Render::ClearColor)
        res |= BGFX_CLEAR_COLOR;
    if (flags & Render::ClearDepth)
        res |= BGFX_CLEAR_DEPTH;
    if (flags & Render::ClearStencil)
        res |= BGFX_CLEAR_STENCIL;
    return res;
}

/// The per-widget frame surface (DrawSurface.h): a contiguous view-id
/// block from the shared granule pool (so surface passes never collide
/// with the viewers'), the surface's own backbuffer target, and the
/// frame plumbing proven by the engine -- submissions happen on the
/// widget's context (bgfx encoding is CPU-side), endFrame runs
/// bgfx::frame() on the library context and blits the finished colour
/// into whatever framebuffer the widget had bound at beginFrame.
class BGFXDrawSurface : public Render::DrawSurface {
public:
    QOpenGLWidget *widget = nullptr;
    uint16_t baseId = 0;
    uint16_t idSpan = 0;
    unsigned numPasses = 0;
    int width = 0;
    int height = 0;
    int hostFbo = 0;
    bool inFrame = false;

    struct PassConfig {
        Render::TargetHandle target;
        bool haveRect = false;
        int rect[4] = {0, 0, 0, 0};
        uint32_t clearRgba = 0;
        float clearDepth = 1.0f;
        uint8_t clearStencil = 0;
        uint16_t clearFlags = BGFX_CLEAR_NONE;
        bool sequential = false;
        bool haveTransform = false;
        float view[16];
        float proj[16];
    };
    std::vector<PassConfig> passes;

    bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle backbuffer = BGFX_INVALID_HANDLE;
    unsigned blitFbo = 0;
    /// First-presentation marker for the one-shot log below -- the
    /// only sign outside a debugger that a facade consumer's frames
    /// are going through the backend at all.
    bool presented = false;

    bool deviceUp() const
    {
        return _BGFXLib.currentType != bgfx::RendererType::Noop;
    }

    ~BGFXDrawSurface() override
    {
        destroyBackbuffer();
        _BGFXLib.releaseIds(baseId, idSpan);
    }

    void destroyBackbuffer()
    {
        if (deviceUp()) {
            if (bgfx::isValid(backbuffer))
                bgfx::destroy(backbuffer);
            if (bgfx::isValid(color))
                bgfx::destroy(color);
            if (bgfx::isValid(depth))
                bgfx::destroy(depth);
        }
        backbuffer = BGFX_INVALID_HANDLE;
        color = BGFX_INVALID_HANDLE;
        depth = BGFX_INVALID_HANDLE;
        // The blit wrapper belongs to the widget's GL context; without
        // a current context in the share group it is leaked, which
        // only happens on teardown paths where the context is gone
        // anyway.
        if (blitFbo && QOpenGLContext::currentContext()) {
            GLuint fbo = blitFbo;
            QOpenGLContext::currentContext()->extraFunctions()
                ->glDeleteFramebuffers(1, &fbo);
        }
        blitFbo = 0;
    }

    void applyPass(unsigned pass)
    {
        const auto &conf = passes[pass];
        const bgfx::ViewId id = bgfx::ViewId(baseId + pass);
        bgfx::setViewFrameBuffer(id, conf.target.valid()
                ? bgfx::FrameBufferHandle{conf.target.idx} : backbuffer);
        if (conf.haveRect)
            bgfx::setViewRect(id, uint16_t(conf.rect[0]),
                              uint16_t(conf.rect[1]),
                              uint16_t(conf.rect[2]),
                              uint16_t(conf.rect[3]));
        else
            bgfx::setViewRect(id, 0, 0, uint16_t(width), uint16_t(height));
        // The clear runs only when the pass draws in a frame: bgfx
        // applies a view's clear when its first item executes, and an
        // empty view is skipped -- which is the facade's contract.
        bgfx::setViewClear(id, conf.clearFlags, conf.clearRgba,
                           conf.clearDepth, conf.clearStencil);
        bgfx::setViewMode(id, conf.sequential
                ? bgfx::ViewMode::Sequential : bgfx::ViewMode::Default);
        if (conf.haveTransform)
            bgfx::setViewTransform(id, conf.view, conf.proj);
    }

    bool beginFrame(int w, int h) override
    {
        if (!deviceUp() || w <= 0 || h <= 0 || !idSpan)
            return false;
        auto *ctx = QOpenGLContext::currentContext();
        if (!ctx)
            return false;
        // Whatever the widget has bound now is where endFrame's blit
        // must land (QOpenGLWidget's own framebuffer normally).
        GLint fbo = 0;
        ctx->extraFunctions()->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        hostFbo = fbo;
        if (w != width || h != height) {
            destroyBackbuffer();
            width = w;
            height = h;
            color = bgfx::createTexture2D(uint16_t(w), uint16_t(h), false,
                                          1, bgfx::TextureFormat::RGBA8,
                                          BGFX_TEXTURE_RT, nullptr);
            depth = bgfx::createTexture2D(uint16_t(w), uint16_t(h), false,
                                          1, bgfx::TextureFormat::D24S8,
                                          BGFX_TEXTURE_RT, nullptr);
            bgfx::TextureHandle attachments[] = {color, depth};
            backbuffer = bgfx::createFrameBuffer(2, attachments, false);
        }
        for (unsigned p = 0; p < numPasses; ++p)
            applyPass(p);
        inFrame = true;
        return true;
    }

    void endFrame() override
    {
        if (!inFrame)
            return;
        inFrame = false;
        // The context dance the engine frame uses: bgfx draws through
        // the library's context, the blit lands on the widget's.
        widget->doneCurrent();
        _BGFXLib.makeCurrent();
        bgfx::frame();
        widget->makeCurrent();
        auto *f = QOpenGLContext::currentContext()->extraFunctions();
        if (!blitFbo) {
            // getInternal is valid only after the frame that created
            // the texture ran, which the bgfx::frame() above ensured.
            GLuint colorId = bgfx::getInternal(color);
            GLuint fbo = 0;
            f->glGenFramebuffers(1, &fbo);
            f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_TEXTURE_2D, colorId, 0);
            blitFbo = fbo;
        }
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(hostFbo));
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, blitFbo);
        f->glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                             GL_COLOR_BUFFER_BIT, GL_NEAREST);
        f->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(hostFbo));
        if (!presented) {
            presented = true;
            std::printf("bgfx: draw surface first frame %dx%d "
                        "(passes %u at %u)\n",
                        width, height, numPasses, unsigned(baseId));
        }
    }

    void setPassTarget(unsigned pass, Render::TargetHandle target) override
    {
        if (pass >= numPasses)
            return;
        passes[pass].target = target;
        if (inFrame)
            applyPass(pass);
    }

    void setPassRect(unsigned pass, int x, int y, int w, int h) override
    {
        if (pass >= numPasses)
            return;
        auto &conf = passes[pass];
        conf.haveRect = true;
        conf.rect[0] = x;
        conf.rect[1] = y;
        conf.rect[2] = w;
        conf.rect[3] = h;
        if (inFrame)
            applyPass(pass);
    }

    void setPassClear(unsigned pass, uint32_t rgba, float depthVal,
                      uint8_t stencil, Render::ClearFlags flags) override
    {
        if (pass >= numPasses)
            return;
        auto &conf = passes[pass];
        conf.clearRgba = rgba;
        conf.clearDepth = depthVal;
        conf.clearStencil = stencil;
        conf.clearFlags = toBgfxClearFlags(flags);
        if (inFrame)
            applyPass(pass);
    }

    void setPassSequential(unsigned pass, bool on) override
    {
        if (pass >= numPasses)
            return;
        passes[pass].sequential = on;
        if (inFrame)
            applyPass(pass);
    }

    void setPassTransform(unsigned pass, const float view[16],
                          const float proj[16]) override
    {
        if (pass >= numPasses)
            return;
        auto &conf = passes[pass];
        conf.haveTransform = true;
        for (int i = 0; i < 16; ++i) {
            conf.view[i] = view[i];
            conf.proj[i] = proj[i];
        }
        if (inFrame)
            applyPass(pass);
    }

    void setUniform(Render::UniformHandle handle, const void *value,
                    uint16_t num) override
    {
        if (handle.valid() && value)
            bgfx::setUniform({handle.idx}, value, num);
    }

    void setTexture(uint8_t stage, Render::UniformHandle sampler,
                    Render::TextureHandle texture) override
    {
        if (sampler.valid() && texture.valid())
            bgfx::setTexture(stage, {sampler.idx},
                             bgfx::TextureHandle{texture.idx});
    }

    void setTransform(const float model[16]) override
    {
        bgfx::setTransform(model);
    }

    void setState(const Render::DrawState &state) override
    {
        bgfx::setState(toBgfxState(state));
    }

    void setStencil(const Render::StencilState &stencil) override
    {
        // One state for both faces (the facade does not split them);
        // BGFX_STENCIL_NONE as the back state means "same as front".
        bgfx::setStencil(toBgfxStencil(stencil));
    }

    void setVertexBuffer(Render::VertexBufferHandle handle) override
    {
        if (handle.valid())
            bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{handle.idx});
    }

    void setIndexBuffer(Render::IndexBufferHandle handle) override
    {
        if (handle.valid())
            bgfx::setIndexBuffer(bgfx::IndexBufferHandle{handle.idx});
    }

    void submit(unsigned pass, Render::ProgramHandle program) override
    {
        if (!inFrame || pass >= numPasses || !program.valid()) {
            // The encoder state set for this draw must not leak into
            // the next one when the draw itself is refused.
            bgfx::discard();
            return;
        }
        bgfx::submit(bgfx::ViewId(baseId + pass),
                     bgfx::ProgramHandle{program.idx});
    }

    Render::TextureHandle runEffect(Render::EffectHandle effect,
                                    unsigned firstPass,
                                    Render::TextureHandle normalZ,
                                    const Render::EffectParams &params)
                                    override
    {
        // The effect tier arrives with step 7 of the port.
        (void)effect;
        (void)firstPass;
        (void)normalZ;
        (void)params;
        return {};
    }
};

class BGFXDrawDevice : public Render::DrawDevice {
public:
    bool available() const override
    {
        return _BGFXLib.currentType != bgfx::RendererType::Noop;
    }

    Render::VertexBufferHandle createVertexBuffer(
            const void *data, uint32_t bytes,
            const Render::VertexLayout &layout) override
    {
        if (!available() || !data || !bytes)
            return {};
        auto handle = bgfx::createVertexBuffer(bgfx::copy(data, bytes),
                                               toBgfxLayout(layout));
        return {handle.idx};
    }

    Render::IndexBufferHandle createIndexBuffer(const void *data,
                                                uint32_t bytes,
                                                bool int32) override
    {
        if (!available() || !data || !bytes)
            return {};
        auto handle = bgfx::createIndexBuffer(
                bgfx::copy(data, bytes),
                int32 ? BGFX_BUFFER_INDEX32 : BGFX_BUFFER_NONE);
        return {handle.idx};
    }

    Render::ProgramHandle createProgram(const char *vsName,
                                        const char *fsName) override
    {
        if (!available() || !vsName)
            return {};
        // The stock pack loader: reports a missing stage and returns
        // invalid instead of asserting, and never orphans the sibling
        // shader of a half-loaded pair. shaderPath() is the engine's
        // own asset root, FC_BGFX_SHADER_DIR override included, so
        // facade consumers hot-reload the same way the engine does.
        auto handle = fcLoadProgram(vsName, fsName,
                                    _BGFXLib.shaderPath().c_str());
        return {handle.idx};
    }

    Render::UniformHandle createUniform(const char *name,
                                        Render::UniformType type,
                                        uint16_t num) override
    {
        if (!available() || !name)
            return {};
        auto handle = bgfx::createUniform(name, toBgfxUniformType(type),
                                          num);
        return {handle.idx};
    }

    Render::TextureHandle createTexture2D(int width, int height,
                                          Render::DrawTextureFormat format,
                                          uint32_t flags,
                                          const void *data) override
    {
        if (!available() || width <= 0 || height <= 0)
            return {};
        const bgfx::Memory *mem = nullptr;
        if (data)
            mem = bgfx::copy(data, uint32_t(width) * uint32_t(height)
                             * bytesPerTexel(format));
        auto handle = bgfx::createTexture2D(uint16_t(width),
                                            uint16_t(height), false, 1,
                                            toBgfxFormat(format),
                                            toBgfxSamplerFlags(flags), mem);
        return {handle.idx};
    }

    Render::TextureHandle createRenderTexture(
            int width, int height, Render::DrawTextureFormat format,
            uint32_t flags) override
    {
        if (!available() || width <= 0 || height <= 0)
            return {};
        auto handle = bgfx::createTexture2D(
                uint16_t(width), uint16_t(height), false, 1,
                toBgfxFormat(format),
                BGFX_TEXTURE_RT | toBgfxSamplerFlags(flags), nullptr);
        return {handle.idx};
    }

    Render::TargetHandle createTarget(
            const Render::TextureHandle *colors, int numColors,
            Render::TextureHandle depthStencil) override
    {
        if (!available() || numColors < 0)
            return {};
        bgfx::TextureHandle attachments[9];
        int num = 0;
        for (int i = 0; i < numColors
                && num < int(sizeof(attachments) / sizeof(attachments[0]));
                ++i) {
            if (!colors[i].valid())
                return {};
            attachments[num++] = {colors[i].idx};
        }
        if (depthStencil.valid())
            attachments[num++] = {depthStencil.idx};
        if (!num)
            return {};
        auto handle = bgfx::createFrameBuffer(uint8_t(num), attachments,
                                              false);
        return {handle.idx};
    }

    void destroy(Render::VertexBufferHandle handle) override
    {
        if (handle.valid())
            bgfx::destroy(bgfx::VertexBufferHandle{handle.idx});
    }
    void destroy(Render::IndexBufferHandle handle) override
    {
        if (handle.valid())
            bgfx::destroy(bgfx::IndexBufferHandle{handle.idx});
    }
    void destroy(Render::ProgramHandle handle) override
    {
        if (handle.valid())
            bgfx::destroy(bgfx::ProgramHandle{handle.idx});
    }
    void destroy(Render::UniformHandle handle) override
    {
        if (handle.valid())
            bgfx::destroy(bgfx::UniformHandle{handle.idx});
    }
    void destroy(Render::TextureHandle handle) override
    {
        if (handle.valid())
            bgfx::destroy(bgfx::TextureHandle{handle.idx});
    }
    void destroy(Render::TargetHandle handle) override
    {
        if (handle.valid())
            bgfx::destroy(bgfx::FrameBufferHandle{handle.idx});
    }

    Render::EffectHandle createEffect(Render::EffectType type) override
    {
        // The effect tier arrives with step 7 of the port (the AO
        // service); until then no effect exists to instantiate.
        (void)type;
        return {};
    }
    void destroy(Render::EffectHandle handle) override
    {
        (void)handle;
    }

    std::unique_ptr<Render::DrawSurface> createSurface(
            QOpenGLWidget *widget, unsigned numPasses) override
    {
        if (!available() || !widget || !numPasses
                || numPasses > BGFX_CONFIG_MAX_VIEWS)
            return nullptr;
        auto surface = std::make_unique<BGFXDrawSurface>();
        surface->widget = widget;
        surface->numPasses = numPasses;
        surface->passes.resize(numPasses);
        if (!_BGFXLib.reserveIds(surface->baseId, surface->idSpan,
                                 uint16_t(numPasses)))
            return nullptr;
        return surface;
    }
};

BGFXDrawDevice _drawDevice;

} // namespace

namespace Render {

DrawDevice *fcBGFXDrawDevice()
{
    return &_drawDevice;
}

} // namespace Render
