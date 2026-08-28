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

#ifndef RENDER_FRAME_IMAGE_CONSUMER_H
#define RENDER_FRAME_IMAGE_CONSUMER_H

#include <cstdint>
#include <memory>

#include "Renderer.h"

namespace Render {

/// A FrameConsumer that shows an image: the blit of docs/
/// CyclesIntegration.md sec 5.2 and 5.7, on its own so that the same
/// pass serves a frame from anywhere -- the desktop's Cycles viewport
/// hands it the half4 frame its session staged, the browser viewer the
/// RGBA8 frame it decoded off the wire (sec 7.1). One pass in the
/// scene run: the engine's fullscreen triangle into hostTarget(),
/// depth test and write off, premultiplied blend; the host's raster
/// draws only depth under it and lines, selection and overlays over it
/// (setExternalBaseLayer).
///
/// Colour: the image is either LINEAR light (a path tracer's frame) or
/// sRGB-encoded (an 8-bit frame off the wire), and the host target is
/// either linear (colour managed) or display space. The blit encodes
/// or decodes so that what lands in the target is in the target's
/// space -- the sec 6.1 rule, both ways.
class RendererExport FrameImageConsumer : public FrameConsumer
{
public:
    enum class Format : uint8_t {
        RGBA8,     ///< 4 bytes a texel
        RGBA16F,   ///< 4 half floats a texel, premultiplied
    };

    FrameImageConsumer();
    ~FrameImageConsumer() override;

    /// Stage an image, bottom-up rows tightly packed, copied here; the
    /// upload happens in the next drawFrame(). \a linear says the
    /// pixels are linear light rather than sRGB-encoded. Host thread.
    void setImage(const void *pixels, int width, int height, Format format,
                  bool linear);
    /// Forget the image: drawFrame() draws nothing until the next
    /// setImage().
    void clear();
    bool hasImage() const;
    int width() const;
    int height() const;

    unsigned framePasses() const override
    {
        return 1;
    }
    void drawFrame(DrawSurface &surface) override;

private:
    struct Private;
    std::unique_ptr<Private> pimpl;
};

}  // namespace Render

#endif  // RENDER_FRAME_IMAGE_CONSUMER_H
