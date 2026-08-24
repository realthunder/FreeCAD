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
        // The frame surface arrives with step 6 of the port: pass-id
        // block reservation plus the beginFrame/endFrame context dance
        // proven in BGFXFrame.cpp. Resources-only until then.
        (void)widget;
        (void)numPasses;
        return nullptr;
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
