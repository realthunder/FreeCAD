/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "FrameImageConsumer.h"

#include <cstring>
#include <vector>

#include "DrawDevice.h"
#include "DrawSurface.h"

namespace Render {

struct FrameImageConsumer::Private {
    std::vector<uint8_t> pixels;   ///< the staged image, bottom-up
    int width = 0;
    int height = 0;
    Format format = Format::RGBA8;
    bool linear = false;
    bool dirty = false;            ///< staged since the last upload
    bool have = false;

    ProgramHandle program;
    UniformHandle sampler;
    UniformHandle params;
    VertexBufferHandle quad;
    TextureHandle texture;
    int texWidth = 0;
    int texHeight = 0;
    Format texFormat = Format::RGBA8;

    ~Private()
    {
        if (auto *dev = DrawDevice::instance()) {
            if (texture.valid())
                dev->destroy(texture);
            if (program.valid())
                dev->destroy(program);
            if (sampler.valid())
                dev->destroy(sampler);
            if (params.valid())
                dev->destroy(params);
            if (quad.valid())
                dev->destroy(quad);
        }
    }

    bool ensureResources(DrawDevice *dev)
    {
        if (program.valid())
            return true;
        program = dev->createProgram("vs_fc_comp", "fs_fc_cycles_blit");
        sampler = dev->createUniform("s_cyclesImage", UniformType::Sampler);
        params = dev->createUniform("u_cyclesBlit", UniformType::Vec4);
        // The engine's own fullscreen triangle: three vertices whose
        // clipped extent covers the target.
        static const float tri[9] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
        VertexLayout layout;
        layout.add(DrawAttrib::Position, 3, DrawAttribType::Float);
        quad = dev->createVertexBuffer(tri, sizeof(tri), layout);
        return program.valid() && sampler.valid() && params.valid() && quad.valid();
    }

    static uint32_t texelBytes(Format f)
    {
        return f == Format::RGBA16F ? 8u : 4u;
    }
};

FrameImageConsumer::FrameImageConsumer()
    : pimpl(new Private)
{}

FrameImageConsumer::~FrameImageConsumer() = default;

void FrameImageConsumer::setImage(const void *pixels, int width, int height,
                                  Format format, bool linear)
{
    if (!pixels || width <= 0 || height <= 0)
        return;
    const size_t bytes = size_t(width) * size_t(height) * Private::texelBytes(format);
    pimpl->pixels.resize(bytes);
    std::memcpy(pimpl->pixels.data(), pixels, bytes);
    pimpl->width = width;
    pimpl->height = height;
    pimpl->format = format;
    pimpl->linear = linear;
    pimpl->dirty = true;
    pimpl->have = true;
}

void FrameImageConsumer::clear()
{
    pimpl->have = false;
    pimpl->dirty = false;
    pimpl->pixels.clear();
}

bool FrameImageConsumer::hasImage() const
{
    return pimpl->have;
}

int FrameImageConsumer::width() const
{
    return pimpl->have ? pimpl->width : 0;
}

int FrameImageConsumer::height() const
{
    return pimpl->have ? pimpl->height : 0;
}

void FrameImageConsumer::drawFrame(DrawSurface &surface)
{
    Private &p = *pimpl;
    if (!p.have)
        return;
    auto *dev = DrawDevice::instance();
    if (!dev || !p.ensureResources(dev))
        return;

    if (p.dirty) {
        if (!p.texture.valid() || p.width != p.texWidth || p.height != p.texHeight
            || p.format != p.texFormat) {
            if (p.texture.valid())
                dev->destroy(p.texture);
            // Linear filtering: at full size the quad maps texels 1:1,
            // and at a divided size the interpolation is what makes
            // the coarse pass readable.
            p.texture = dev->createTexture2D(
                p.width, p.height,
                p.format == Format::RGBA16F ? DrawTextureFormat::RGBA16F
                                            : DrawTextureFormat::RGBA8,
                TextureClamp);
            p.texWidth = p.width;
            p.texHeight = p.height;
            p.texFormat = p.format;
        }
        if (p.texture.valid())
            dev->updateTexture2D(p.texture, 0, 0, p.width, p.height, p.pixels.data(),
                                 uint32_t(p.pixels.size()));
        p.dirty = false;
    }
    if (!p.texture.valid())
        return;

    int hw = 0;
    int hh = 0;
    surface.hostSize(hw, hh);
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    surface.setPassTarget(0, surface.hostTarget());
    surface.setPassRect(0, 0, 0, hw, hh);
    surface.setPassClear(0, 0, 1.0f, 0, ClearNone);
    surface.setPassSequential(0, false);
    surface.setPassTransform(0, identity, identity);
    // x: encode a linear image for a display-space host target;
    // y: decode an encoded image for a linear one (sec 6.1, both ways).
    const bool hostLinear = surface.hostLinearColor();
    const float blit[4] = {p.linear && !hostLinear ? 1.0f : 0.0f,
                           !p.linear && hostLinear ? 1.0f : 0.0f, 0, 0};
    surface.setUniform(p.params, blit);
    surface.setTexture(0, p.sampler, p.texture);
    surface.setTransform(identity);
    surface.setVertexBuffer(p.quad);
    DrawState state;
    state.depthWrite = false;
    state.depthFunc = CompareFunc::Always;
    state.blend = BlendMode::Premultiplied;
    surface.setState(state);
    surface.submit(0, p.program);
}

}  // namespace Render
