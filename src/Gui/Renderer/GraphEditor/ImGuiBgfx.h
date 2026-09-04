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

#ifndef RENDERER_GRAPHEDITOR_IMGUIBGFX_H
#define RENDERER_GRAPHEDITOR_IMGUIBGFX_H

/// \file ImGuiBgfx.h
/// One Dear ImGui context drawn by bgfx (docs/ShaderGraphEditor.md
/// sec 4.3). This is bgfx's own example renderer
/// (bgfx/examples/common/imgui/imgui.cpp) reduced to what a Qt-hosted
/// surface needs: no entry:: key map, no global singleton -- one
/// instance per surface, each with its own ImGui context -- and the
/// view id handed in per frame by the surface that owns the pass.
///
/// Compiled only inside FreeCADRenderer: it speaks bgfx directly, on
/// the strength of transient buffers and the embedded ImGui shaders,
/// which the draw facade does not offer.

#include <cstdint>
#include <memory>

struct ImGuiContext;

namespace Render {

class ImGuiBgfx {
public:
    ImGuiBgfx();
    ~ImGuiBgfx();

    /// Create the context, its font atlas and the bgfx programs.
    /// bgfx must be up. False when it is not.
    bool create(float fontSize);
    /// Release everything; bgfx must still be up for the handles to
    /// be freed, otherwise they went down with the device.
    void destroy(bool deviceUp);
    bool valid() const;

    /// Pack a bgfx texture (by its handle index, as a DrawDevice
    /// TextureHandle carries it) into the 64-bit texture id ImGui::Image
    /// takes here: the same packing as bgfx's example, so a texture any
    /// consumer of this renderer made draws through this backend.
    static uint64_t packTexture(uint16_t textureIdx);

    ImGuiContext *context() const;
    /// ImGui's current context is a global; every call into ImGui on
    /// behalf of this instance is bracketed by this.
    void makeCurrent();

    /// Begin a frame over a \a width x \a height logical-pixel display
    /// drawn at \a pixelRatio, \a deltaTime seconds after the last.
    void newFrame(float width, float height, float pixelRatio,
                  float deltaTime);
    /// End the frame and submit its draw lists to bgfx view \a viewId,
    /// whose target is \a fbWidth x \a fbHeight framebuffer pixels.
    void render(uint16_t viewId, int fbWidth, int fbHeight);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Render

#endif // RENDERER_GRAPHEDITOR_IMGUIBGFX_H
