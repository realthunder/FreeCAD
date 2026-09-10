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
#include <cstring>
#include <map>

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
    case Render::DrawTextureFormat::RGBA16F:
        return bgfx::TextureFormat::RGBA16F;
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
    case Render::DrawTextureFormat::RGBA16F:
        return 8;
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
    else if (state.blend == Render::BlendMode::Premultiplied)
        res |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                     BGFX_STATE_BLEND_INV_SRC_ALPHA);
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

/// One AO effect instance (the facade effect tier's first service;
/// docs/CAMSimRenderPort.md step 7). Its own copy of the engine's AO
/// resolve chain -- targets, noise, depth pyramid, and the submit
/// sequence of BGFXView::submitAOResolve -- instantiated per consumer
/// instead of refactoring the view's members out from under the
/// engine. The programs and shaders ARE the engine's (same pack);
/// keep the submit sequence in step with BGFXViewEffects.cpp when
/// either changes.
class BGFXAOEffect {
public:
    int width = 0;
    int height = 0;
    static constexpr int kMipLevels = 6;
    static constexpr int kSamples = 16;
    bgfx::TextureHandle aoTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoBlurTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle noiseTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle genFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle blurFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle mipTex[kMipLevels];
    bgfx::FrameBufferHandle mipFbo[kMipLevels];
    int mipCount = 0;
    bgfx::ProgramHandle progSsao = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle progGtao = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle progGtaoDepth = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle progGtaoBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle progSsaoBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoKernel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texNormalZ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAONoise = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAO = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAOMip[kMipLevels];

    BGFXAOEffect()
    {
        for (int m = 0; m < kMipLevels; ++m) {
            mipTex[m] = BGFX_INVALID_HANDLE;
            mipFbo[m] = BGFX_INVALID_HANDLE;
            s_texAOMip[m] = BGFX_INVALID_HANDLE;
        }
    }

    ~BGFXAOEffect()
    {
        destroyTargets();
        if (_BGFXLib.currentType == bgfx::RendererType::Noop)
            return;
        bgfx::ProgramHandle progs[] = {progSsao, progGtao, progGtaoDepth,
                                       progGtaoBlur, progSsaoBlur};
        for (auto h : progs)
            if (bgfx::isValid(h))
                bgfx::destroy(h);
        bgfx::UniformHandle unis[] = {u_aoParams, u_aoParams2, u_aoKernel,
                                      s_texNormalZ, s_texAONoise, s_texAO};
        for (auto h : unis)
            if (bgfx::isValid(h))
                bgfx::destroy(h);
        for (int m = 0; m < kMipLevels; ++m)
            if (bgfx::isValid(s_texAOMip[m]))
                bgfx::destroy(s_texAOMip[m]);
    }

    bool init()
    {
        // By value: shaderPath() returns a temporary, and its c_str()
        // must outlive all five loads.
        const std::string path = _BGFXLib.shaderPath();
        progSsao = fcLoadProgram("vs_fc_comp", "fs_fc_ssao", path.c_str());
        progGtao = fcLoadProgram("vs_fc_comp", "fs_fc_gtao", path.c_str());
        progGtaoDepth = fcLoadProgram("vs_fc_comp", "fs_fc_gtao_depths",
                                      path.c_str());
        progGtaoBlur = fcLoadProgram("vs_fc_comp", "fs_fc_gtao_blur",
                                     path.c_str());
        progSsaoBlur = fcLoadProgram("vs_fc_comp", "fs_fc_ssao_blur",
                                     path.c_str());
        if (!bgfx::isValid(progSsao) || !bgfx::isValid(progSsaoBlur))
            return false;
        u_aoParams = bgfx::createUniform("u_aoParams",
                                         bgfx::UniformType::Vec4);
        u_aoParams2 = bgfx::createUniform("u_aoParams2",
                                          bgfx::UniformType::Vec4);
        u_aoKernel = bgfx::createUniform("u_aoKernel",
                                         bgfx::UniformType::Vec4, kSamples);
        s_texNormalZ = bgfx::createUniform("s_texNormalZ",
                                           bgfx::UniformType::Sampler);
        s_texAONoise = bgfx::createUniform("s_texAONoise",
                                           bgfx::UniformType::Sampler);
        s_texAO = bgfx::createUniform("s_texAO",
                                      bgfx::UniformType::Sampler);
        for (int m = 0; m < kMipLevels; ++m) {
            // 1-based, matching the fs_fc_gtao sampler declarations.
            char name[24];
            std::snprintf(name, sizeof(name), "s_texAOMip%d", m + 1);
            s_texAOMip[m] = bgfx::createUniform(name,
                                                bgfx::UniformType::Sampler);
        }
        return true;
    }

    void destroyTargets()
    {
        if (_BGFXLib.currentType != bgfx::RendererType::Noop) {
            if (bgfx::isValid(genFbo))
                bgfx::destroy(genFbo);
            if (bgfx::isValid(blurFbo))
                bgfx::destroy(blurFbo);
            if (bgfx::isValid(aoTex))
                bgfx::destroy(aoTex);
            if (bgfx::isValid(aoBlurTex))
                bgfx::destroy(aoBlurTex);
            if (bgfx::isValid(noiseTex))
                bgfx::destroy(noiseTex);
            for (int m = 0; m < kMipLevels; ++m) {
                if (bgfx::isValid(mipFbo[m]))
                    bgfx::destroy(mipFbo[m]);
                if (bgfx::isValid(mipTex[m]))
                    bgfx::destroy(mipTex[m]);
                mipFbo[m] = BGFX_INVALID_HANDLE;
                mipTex[m] = BGFX_INVALID_HANDLE;
            }
        }
        genFbo = blurFbo = BGFX_INVALID_HANDLE;
        aoTex = aoBlurTex = noiseTex = BGFX_INVALID_HANDLE;
        mipCount = 0;
        width = height = 0;
    }

    bool ensureTargets(int w, int h)
    {
        if (w == width && h == height && bgfx::isValid(aoTex))
            return true;
        destroyTargets();
        width = w;
        height = h;
        const uint64_t resFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;   // linear
        aoTex = bgfx::createTexture2D(uint16_t(w), uint16_t(h), false, 1,
                                      bgfx::TextureFormat::R8, resFlags);
        aoBlurTex = bgfx::createTexture2D(uint16_t(w), uint16_t(h), false,
                                          1, bgfx::TextureFormat::R8,
                                          resFlags);
        if (!bgfx::isValid(aoTex) || !bgfx::isValid(aoBlurTex))
            return false;
        genFbo = bgfx::createFrameBuffer(1, &aoTex, false);
        blurFbo = bgfx::createFrameBuffer(1, &aoBlurTex, false);
        if (!bgfx::isValid(genFbo) || !bgfx::isValid(blurFbo))
            return false;
        // The engine's fixed 4x4 noise: rotation vectors in .xy plus a
        // Bayer dither in .z (BGFXViewLifecycle.cpp keeps the story).
        static const uint8_t noise[64] = {
            0xa2, 0x05, 0x00, 0xff, 0x11, 0xc0, 0x88, 0xff,
            0xee, 0xc0, 0x22, 0xff, 0x25, 0xd9, 0xaa, 0xff,
            0x27, 0x23, 0xcc, 0xff, 0x63, 0x03, 0x44, 0xff,
            0x20, 0xd4, 0xee, 0xff, 0x2a, 0xde, 0x66, 0xff,
            0x90, 0xfe, 0x33, 0xff, 0x9b, 0xfc, 0xbb, 0xff,
            0xae, 0x09, 0x11, 0xff, 0xef, 0xbe, 0x99, 0xff,
            0xd8, 0x23, 0xff, 0xff, 0x3a, 0x15, 0x77, 0xff,
            0x00, 0x87, 0xdd, 0xff, 0xe8, 0xc9, 0x55, 0xff,
        };
        noiseTex = bgfx::createTexture2D(4, 4, false, 1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT,
            bgfx::copy(noise, sizeof(noise)));
        const auto *caps = bgfx::getCaps();
        const uint64_t mipFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const bool mipR32 = 0 != (caps->formats[bgfx::TextureFormat::R32F]
                                  & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        const bool mipR16 = 0 != (caps->formats[bgfx::TextureFormat::R16F]
                                  & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        mipCount = (mipR32 || mipR16) && bgfx::isValid(progGtaoDepth)
            ? kMipLevels : 0;
        for (int m = 0; m < mipCount; ++m) {
            const uint16_t mw = uint16_t(std::max(1, w >> (m + 1)));
            const uint16_t mh = uint16_t(std::max(1, h >> (m + 1)));
            mipTex[m] = bgfx::createTexture2D(mw, mh, false, 1,
                mipR32 ? bgfx::TextureFormat::R32F
                       : bgfx::TextureFormat::R16F, mipFlags);
            if (bgfx::isValid(mipTex[m]))
                mipFbo[m] = bgfx::createFrameBuffer(1, &mipTex[m], false);
            if (!bgfx::isValid(mipFbo[m])) {
                mipCount = m;
                break;
            }
        }
        return true;
    }

    void fullscreen(bgfx::ViewId id, bgfx::ProgramHandle prog)
    {
        TransientVertex::init();
        if (bgfx::getAvailTransientVertexBuffer(
                    3, TransientVertex::ms_layout) < 3)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 3,
                                         TransientVertex::ms_layout);
        auto *v = reinterpret_cast<TransientVertex *>(tvb.data);
        v[0] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        v[1] = { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        v[2] = {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(id, prog);
    }

    void configureView(bgfx::ViewId id, bgfx::FrameBufferHandle fb,
                       int w, int h, const float *proj)
    {
        static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                        0, 0, 1, 0, 0, 0, 0, 1};
        bgfx::setViewFrameBuffer(id, fb);
        bgfx::setViewRect(id, 0, 0, uint16_t(w), uint16_t(h));
        bgfx::setViewClear(id, BGFX_CLEAR_NONE);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::setViewTransform(id, ident, proj);
    }

    /// The engine's AO resolve sequence (submitAOResolve with
    /// temporalIndex 0 and no interaction fast path), drawn into the
    /// surface's pass range starting at baseId. Returns the result
    /// texture: aoTex for GTAO (the denoise ping-pongs back into it),
    /// aoBlurTex for the classic chain.
    Render::TextureHandle run(uint16_t firstViewId, int w, int h,
                              bgfx::TextureHandle normalZ,
                              const Render::EffectParams &params)
    {
        if (!params.proj || !bgfx::isValid(normalZ) || w <= 0 || h <= 0)
            return {};
        if (!ensureTargets(w, h))
            return {};
        static const float kernel[kSamples][4] = {
            {-0.058091f, 0.018602f, 0.079242f, 0.0f},
            {-0.016977f, 0.100367f, 0.018809f, 0.0f},
            {-0.042287f, 0.079676f, 0.069813f, 0.0f},
            {0.010341f, 0.119322f, 0.054631f, 0.0f},
            {0.012528f, 0.147272f, 0.050676f, 0.0f},
            {-0.131686f, -0.100976f, 0.088122f, 0.0f},
            {0.120937f, 0.161185f, 0.103557f, 0.0f},
            {0.024414f, -0.112444f, 0.246757f, 0.0f},
            {-0.050206f, -0.180815f, 0.265349f, 0.0f},
            {0.057177f, 0.368457f, 0.094947f, 0.0f},
            {0.223564f, 0.320370f, 0.226476f, 0.0f},
            {0.173264f, -0.484121f, 0.107897f, 0.0f},
            {0.046909f, 0.076361f, 0.599589f, 0.0f},
            {0.263978f, 0.433148f, 0.473845f, 0.0f},
            {-0.768430f, 0.171215f, 0.053107f, 0.0f},
            {0.429038f, 0.201413f, 0.754499f, 0.0f},
        };
        const bool gtao = params.method != 0 && bgfx::isValid(progGtao);
        const float radius = params.radius > 0.0f ? params.radius : 10.0f;
        const float intensity =
            params.intensity > 0.0f ? params.intensity : 1.0f;
        const float aoPower = gtao ? 2.2f : 2.5f;
        bgfx::ViewId id = firstViewId;
        const bool depthMips = gtao && mipCount > 0;
        if (depthMips) {
            uint16_t sw = uint16_t(w);
            uint16_t sh = uint16_t(h);
            for (int m = 0; m < mipCount; ++m) {
                configureView(id, mipFbo[m], std::max(1, w >> (m + 1)),
                              std::max(1, h >> (m + 1)), params.proj);
                float dparams[4] = {m == 0 ? 0.0f : 1.0f, radius,
                                    float(sw), float(sh)};
                bgfx::setUniform(u_aoParams, dparams);
                bgfx::setTexture(0, s_texNormalZ,
                                 m == 0 ? normalZ : mipTex[m - 1]);
                fullscreen(id, progGtaoDepth);
                ++id;
                sw = uint16_t(std::max(1, w >> (m + 1)));
                sh = uint16_t(std::max(1, h >> (m + 1)));
            }
        }
        // The input is RGBA32F, so the fp16 coplanarity-guard flag
        // (paramZ bit 1) stays clear; no interaction fast path and no
        // temporal index for a facade run.
        const float paramZ = gtao ? 0.0f : 0.02f * radius;
        float genParams[4] = {radius, intensity, paramZ, aoPower};
        bgfx::setUniform(u_aoParams, genParams);
        if (gtao) {
            float params2[4] = {9.0f, 6.0f,
                                depthMips ? float(mipCount) : 0.0f, 1.0f};
            bgfx::setUniform(u_aoParams2, params2);
            for (int m = 0; m < kMipLevels; ++m)
                bgfx::setTexture(uint8_t(2 + m), s_texAOMip[m],
                                 depthMips ? mipTex[m] : normalZ);
        }
        else {
            float params2[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(u_aoParams2, params2);
            bgfx::setUniform(u_aoKernel, kernel, kSamples);
        }
        bgfx::setTexture(0, s_texNormalZ, normalZ);
        bgfx::setTexture(1, s_texAONoise, noiseTex);
        configureView(id, genFbo, w, h, params.proj);
        fullscreen(id, gtao ? progGtao : progSsao);
        ++id;

        bgfx::setTexture(0, s_texAO, aoTex);
        if (gtao && bgfx::isValid(progGtaoBlur)) {
            bgfx::setTexture(1, s_texNormalZ, normalZ);
            configureView(id, blurFbo, w, h, params.proj);
            fullscreen(id, progGtaoBlur);
            ++id;
            bgfx::setTexture(0, s_texAO, aoBlurTex);
            bgfx::setTexture(1, s_texNormalZ, normalZ);
            configureView(id, genFbo, w, h, params.proj);
            fullscreen(id, progGtaoBlur);
            return {aoTex.idx};
        }
        configureView(id, blurFbo, w, h, params.proj);
        fullscreen(id, progSsaoBlur);
        return {aoBlurTex.idx};
    }
};

/// Live effect instances, keyed by the facade EffectHandle id. The
/// device creates and destroys them; a surface's runEffect resolves
/// its handle here.
std::map<uint16_t, std::unique_ptr<BGFXAOEffect>> _aoEffects;
uint16_t _nextEffectId = 0;

/// The frame surface (DrawSurface.h), in both flavours.
///
/// STANDALONE (created by createSurface, owns a QOpenGLWidget): a
/// contiguous view-id block from the shared granule pool -- so surface
/// passes never collide with the viewers' -- the surface's own
/// backbuffer target, and the frame plumbing proven by the engine:
/// submissions happen on the widget's context (bgfx encoding is
/// CPU-side), endFrame runs bgfx::frame() on the library context and
/// blits the finished colour into whatever framebuffer the widget had
/// bound at beginFrame.
///
/// ATTACHED (docs/CAMSimRenderPort.md sec 8, driven through
/// BGFXHostSurface): no widget, no id block and no backbuffer. The
/// pass ids are the host view's own, rebound every frame because
/// mapPasses reassigns them; the default target is the host's scene
/// framebuffer; and there is no frame boundary here at all -- the
/// host's render() owns it. bindFrame/unbindFrame bracket the one
/// FrameConsumer::drawFrame call the ids are valid for, so a consumer
/// that kept the surface can submit nothing outside it.
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

    /// Attached-flavour state; all inert on a standalone surface.
    bool isAttached = false;
    std::vector<uint16_t> hostIds;
    bgfx::FrameBufferHandle hostFb = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle hostColorTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle hostDepthTex = BGFX_INVALID_HANDLE;
    bool hostLinear = false;
    /// The host camera of the bound frame, column-major. Only valid
    /// between bindFrame and unbindFrame -- the host's is a different
    /// camera every frame.
    bool hostCameraValid = false;
    float hostViewMtx[16] = {0};
    float hostProjMtx[16] = {0};

    /// The bgfx view id a pass draws in.
    bgfx::ViewId viewIdOf(unsigned pass) const
    {
        if (isAttached)
            return bgfx::ViewId(pass < hostIds.size() ? hostIds[pass] : 0);
        return bgfx::ViewId(baseId + pass);
    }

    uint16_t nativePassId(unsigned pass) const override
    {
        if (pass >= numPasses)
            return 0xffff;
        if (isAttached && (!inFrame || pass >= hostIds.size()))
            return 0xffff;
        return uint16_t(viewIdOf(pass));
    }

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
#ifndef FC_RENDERER_STANDALONE
        // The blit wrapper belongs to the widget's GL context; without
        // a current context in the share group it is leaked, which
        // only happens on teardown paths where the context is gone
        // anyway.
        if (blitFbo && QOpenGLContext::currentContext()) {
            GLuint fbo = blitFbo;
            QOpenGLContext::currentContext()->extraFunctions()
                ->glDeleteFramebuffers(1, &fbo);
        }
#endif
        blitFbo = 0;
    }

    void applyPass(unsigned pass)
    {
        const auto &conf = passes[pass];
        const bgfx::ViewId id = viewIdOf(pass);
        // A pass given no target of its own draws into the surface's
        // default: its own backbuffer standalone, the host's scene
        // target attached. That is what makes
        // setPassTarget(p, hostTarget()) mean the same thing in both.
        bgfx::setViewFrameBuffer(id, conf.target.valid()
                ? bgfx::FrameBufferHandle{conf.target.idx}
                : (isAttached ? hostFb : backbuffer));
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
        // ! Every pass states its transform, even the ones that want
        // identity. A backend view's transform is STICKY, and an
        // attached surface's ids are the host's -- reassigned every
        // frame from whichever passes are live, so an id that carried
        // the scene camera last frame would silently apply it to a
        // clip-space fullscreen quad this frame and put the draw off
        // screen. (A standalone surface owns its ids and would inherit
        // only from itself, but one rule is cheaper than two.)
        static const float kIdentity[16] = {1, 0, 0, 0,
                                            0, 1, 0, 0,
                                            0, 0, 1, 0,
                                            0, 0, 0, 1};
        if (conf.haveTransform)
            bgfx::setViewTransform(id, conf.view, conf.proj);
        else
            bgfx::setViewTransform(id, kIdentity, kIdentity);
    }

    bool beginFrame(int w, int h) override
    {
        // Attached: the host began the frame and owns its boundary.
        // Answering with whether it is bound lets a consumer's draw
        // code keep the begin/draw/end shape it has standalone.
        if (isAttached) {
            (void)w;
            (void)h;
            return inFrame;
        }
#ifdef FC_RENDERER_STANDALONE
        // The browser tier has no widget to own a surface: only the
        // attached flavour exists there.
        return false;
#else
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
#endif
    }

    void endFrame() override
    {
        // Attached: bgfx::frame() belongs to the host, and calling it
        // here would end the host's frame under it.
        if (isAttached)
            return;
        if (!inFrame)
            return;
        inFrame = false;
#ifndef FC_RENDERER_STANDALONE
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
#endif
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
        bgfx::submit(viewIdOf(pass), bgfx::ProgramHandle{program.idx});
    }

    Render::TextureHandle runEffect(Render::EffectHandle effect,
                                    unsigned firstPass,
                                    Render::TextureHandle normalZ,
                                    const Render::EffectParams &params)
                                    override
    {
        if (!inFrame || !effect.valid() || !normalZ.valid())
            return {};
        if (firstPass + Render::kAOEffectPasses > numPasses)
            return {};
        if (isAttached) {
            // The effect tier hands its run a base id and uses the
            // ids after it, so the ids must be consecutive across the
            // effect's own range. They are whenever the range sits
            // inside ONE consumer run (consecutive enum entries,
            // consecutively mapped) -- but the scene and overlay runs
            // are not contiguous with each other, and a frame path
            // change could break the rule some other way, so it is
            // checked rather than assumed: a violation would scribble
            // into a neighbour's view.
            for (unsigned p = firstPass + 1;
                 p < firstPass + Render::kAOEffectPasses; ++p) {
                if (hostIds[p] != uint16_t(hostIds[p - 1] + 1))
                    return {};
            }
        }
        auto it = _aoEffects.find(effect.idx);
        if (it == _aoEffects.end())
            return {};
        return it->second->run(viewIdOf(firstPass), width, height,
                               bgfx::TextureHandle{normalZ.idx}, params);
    }

    bool attached() const override
    {
        return isAttached;
    }

    Render::TargetHandle hostTarget() const override
    {
        // Standalone: an invalid handle, which setPassTarget already
        // reads as "this surface's own backbuffer".
        if (!isAttached || !bgfx::isValid(hostFb))
            return {};
        return {hostFb.idx};
    }

    Render::TextureHandle hostColor() const override
    {
        const bgfx::TextureHandle t = isAttached ? hostColorTex : color;
        return bgfx::isValid(t) ? Render::TextureHandle{t.idx}
                                : Render::TextureHandle{};
    }

    Render::TextureHandle hostDepth() const override
    {
        const bgfx::TextureHandle t = isAttached ? hostDepthTex : depth;
        return bgfx::isValid(t) ? Render::TextureHandle{t.idx}
                                : Render::TextureHandle{};
    }

    void hostSize(int &w, int &h) const override
    {
        w = width;
        h = height;
    }

    bool hostLinearColor() const override
    {
        return hostLinear;
    }

    bool hostCamera(float view[16], float proj[16]) const override
    {
        if (!isAttached || !hostCameraValid)
            return false;
        std::memcpy(view, hostViewMtx, sizeof(hostViewMtx));
        std::memcpy(proj, hostProjMtx, sizeof(hostProjMtx));
        return true;
    }
};

/// The engine-facing driver of an attached surface
/// (BGFXHostSurface, BGFXRendererP.h). It owns the surface; the frame
/// path owns it.
class BGFXHostSurfaceImpl : public Render::BGFXHostSurface {
public:
    BGFXDrawSurface s;
    int lastW = 0;
    int lastH = 0;

    explicit BGFXHostSurfaceImpl(unsigned numPasses)
    {
        s.isAttached = true;
        s.numPasses = numPasses;
        s.passes.resize(numPasses);
        s.hostIds.assign(numPasses, 0);
    }

    Render::DrawSurface &surface() override
    {
        return s;
    }

    unsigned passes() const override
    {
        return s.numPasses;
    }

    void bindFrame(const FrameBind &bind) override
    {
        if (bind.numIds < s.numPasses)
            return;
        for (unsigned p = 0; p < s.numPasses; ++p)
            s.hostIds[p] = bind.ids[p];
        s.hostFb = bind.target;
        s.hostColorTex = bind.color;
        s.hostDepthTex = bind.depth;
        s.width = bind.width;
        s.height = bind.height;
        s.hostLinear = bind.linearColor;
        s.hostCameraValid = bind.viewMtx && bind.projMtx;
        if (s.hostCameraValid) {
            std::memcpy(s.hostViewMtx, bind.viewMtx, sizeof(s.hostViewMtx));
            std::memcpy(s.hostProjMtx, bind.projMtx, sizeof(s.hostProjMtx));
        }
        s.inFrame = true;
        // The ids move between frames (mapPasses reassigns from what
        // is live), so every pass is re-stated every frame rather than
        // relying on bgfx's sticky per-view state.
        for (unsigned p = 0; p < s.numPasses; ++p)
            s.applyPass(p);
        if (!s.presented || bind.width != lastW || bind.height != lastH) {
            // The attached counterpart of the standalone surface's
            // first-frame line: outside a debugger it is the only sign
            // that a consumer is drawing in a host frame at all, and
            // which ids it got. Restated on a resize, because the
            // first host frame of a session is at the widget's
            // pre-layout size and a consumer that never followed the
            // host's later size would look exactly like one that never
            // drew again.
            s.presented = true;
            lastW = bind.width;
            lastH = bind.height;
            std::printf("bgfx: attached draw surface first frame %dx%d "
                        "(passes %u at %u, linear %d)\n",
                        bind.width, bind.height, s.numPasses,
                        unsigned(bind.ids[0]), int(bind.linearColor));
            std::fflush(stdout);
        }
    }

    void unbindFrame() override
    {
        s.inFrame = false;
        s.hostCameraValid = false;
    }
};

class BGFXDrawDevice : public Render::DrawDevice {
public:
    bool available() const override
    {
        return _BGFXLib.currentType != bgfx::RendererType::Noop;
    }

    bool homogeneousDepth() const override
    {
        const bgfx::Caps *caps = bgfx::getCaps();
        return caps && caps->homogeneousDepth;
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

    void updateTexture2D(Render::TextureHandle texture, int x, int y,
                         int width, int height, const void *data,
                         uint32_t bytes) override
    {
        if (!available() || !texture.valid() || !data || width <= 0
                || height <= 0 || bytes < uint32_t(width) * uint32_t(height))
            return;
        bgfx::TextureHandle handle = {texture.idx};
        bgfx::updateTexture2D(handle, 0, 0, uint16_t(x), uint16_t(y),
                              uint16_t(width), uint16_t(height),
                              bgfx::copy(data, bytes));
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
        if (!available() || type != Render::EffectType::AO)
            return {};
        auto effect = std::make_unique<BGFXAOEffect>();
        if (!effect->init())
            return {};
        // Skip the invalid-handle value; ids otherwise just count up
        // (a session does not create effects in numbers).
        uint16_t id = _nextEffectId++;
        if (id == Render::InvalidDrawId)
            id = _nextEffectId++;
        _aoEffects[id] = std::move(effect);
        return {id};
    }
    void destroy(Render::EffectHandle handle) override
    {
        if (handle.valid())
            _aoEffects.erase(handle.idx);
    }

    std::unique_ptr<Render::DrawSurface> createSurface(
            QOpenGLWidget *widget, unsigned numPasses) override
    {
#ifdef FC_RENDERER_STANDALONE
        // No widgets in the browser tier (beginFrame above).
        (void)widget;
        (void)numPasses;
        return nullptr;
#else
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
#endif
    }
};

BGFXDrawDevice _drawDevice;

} // namespace

namespace Render {

DrawDevice *fcBGFXDrawDevice()
{
    return &_drawDevice;
}

std::unique_ptr<BGFXHostSurface> fcBGFXCreateHostSurface(
        unsigned scenePasses, unsigned overlayPasses)
{
    if (_BGFXLib.currentType == bgfx::RendererType::Noop
            || scenePasses + overlayPasses == 0
            || scenePasses > unsigned(BGFXView::NumConsumerSceneViews)
            || overlayPasses > unsigned(BGFXView::NumConsumerOverlayViews))
        return nullptr;
    return std::make_unique<BGFXHostSurfaceImpl>(scenePasses + overlayPasses);
}

} // namespace Render
