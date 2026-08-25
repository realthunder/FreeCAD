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

#ifndef RENDERER_DRAWSURFACE_H
#define RENDERER_DRAWSURFACE_H

/// \file DrawSurface.h
/// The per-widget half of the immediate-mode draw facade
/// (docs/CAMSimRenderPort.md section 3; the device half is
/// DrawDevice.h). A DrawSurface owns a contiguous block of backend
/// view ids -- "passes" here, numbered 0..numPasses-1 in submission
/// order -- and the frame plumbing that lets a QOpenGLWidget's
/// paintGL draw through the backend: beginFrame targets the widget,
/// endFrame runs the backend frame and blits the result into the
/// widget's framebuffer.

#include "DrawDevice.h"

namespace Render {

class RendererExport DrawSurface {
public:
    /// A surface drawing into \a widget through \a numPasses passes.
    /// Null while DrawDevice::instance() is null, and on failure
    /// (passes exhausted -- every 3D view holds a block too).
    static std::unique_ptr<DrawSurface> create(QOpenGLWidget *widget,
                                               unsigned numPasses);

    virtual ~DrawSurface();

    /// Start a frame at the widget's current size. False when the
    /// backend cannot draw this frame; the caller skips to its
    /// fallback (or draws nothing) and tries again next paint.
    virtual bool beginFrame(int width, int height) = 0;
    /// Run the backend frame and composite the result into the
    /// framebuffer the widget had bound at beginFrame.
    virtual void endFrame() = 0;

    /// Direct \a pass at a target created by the device, or at the
    /// surface's own backbuffer when \a target is invalid (the
    /// default).
    virtual void setPassTarget(unsigned pass, TargetHandle target) = 0;
    virtual void setPassRect(unsigned pass, int x, int y, int w, int h) = 0;
    /// The clear that runs when \a pass first draws in a frame. There
    /// is no immediate clear -- a GL consumer's glClear becomes this,
    /// on the pass that ran first.
    virtual void setPassClear(unsigned pass, uint32_t rgba, float depth,
                              uint8_t stencil, ClearFlags flags) = 0;
    /// Draws in \a pass land in submission order instead of being
    /// sorted. Anything order-dependent -- stencil CSG -- needs this;
    /// getting it wrong gives a plausible-looking wrong image, not an
    /// error.
    virtual void setPassSequential(unsigned pass, bool on) = 0;
    virtual void setPassTransform(unsigned pass, const float view[16],
                                  const float proj[16]) = 0;

    /// Set a uniform's value for the draws that follow. Matrices are
    /// column-major, as in GL.
    virtual void setUniform(UniformHandle handle, const void *value,
                            uint16_t num = 1) = 0;
    virtual void setTexture(uint8_t stage, UniformHandle sampler,
                            TextureHandle texture) = 0;
    /// The model matrix of the next submit, column-major.
    virtual void setTransform(const float model[16]) = 0;
    virtual void setState(const DrawState &state) = 0;
    virtual void setStencil(const StencilState &stencil) = 0;
    virtual void setVertexBuffer(VertexBufferHandle handle) = 0;
    /// Optional per draw; a submit without one draws non-indexed.
    virtual void setIndexBuffer(IndexBufferHandle handle) = 0;
    /// Submit one draw into \a pass with everything set since the last
    /// submit. Set state -- buffers, uniforms, state, stencil -- does
    /// not carry over from draw to draw.
    virtual void submit(unsigned pass, ProgramHandle program) = 0;

    /// Run a built-in effect (DrawDevice::createEffect) inside this
    /// frame. The effect's internal passes draw in this surface's pass
    /// ids starting at \a firstPass -- the consumer reserves that range
    /// for the effect and submits nothing there itself. \a normalZ is
    /// the input the effect type declares; the result texture stays
    /// valid until the effect runs again or is destroyed.
    virtual TextureHandle runEffect(EffectHandle effect, unsigned firstPass,
                                    TextureHandle normalZ,
                                    const EffectParams &params) = 0;
};

} // namespace Render

#endif // RENDERER_DRAWSURFACE_H
