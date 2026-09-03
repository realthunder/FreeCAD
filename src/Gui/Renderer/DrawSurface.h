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
/// The per-frame half of the immediate-mode draw facade
/// (docs/CAMSimRenderPort.md sections 3 and 8; the device half is
/// DrawDevice.h). A DrawSurface offers a block of backend view ids --
/// "passes" here, numbered 0..numPasses-1 in submission order -- and
/// the frame plumbing to draw in them.
///
/// It comes in two flavours. A STANDALONE surface owns a
/// QOpenGLWidget: it holds its own id block and its own backbuffer,
/// and its paintGL drives beginFrame/endFrame, which run the backend
/// frame and blit the result into the widget. An ATTACHED surface
/// draws inside a 3D view's frame instead: its ids come out of that
/// view's own block, its default target is that view's scene target,
/// and the host owns the frame boundary. Only the way a surface is
/// obtained differs -- Render::FrameConsumer hands out the attached
/// ones -- and everything below reads the same on either.

#include "DrawDevice.h"

namespace Render {

class RendererExport DrawSurface {
public:
    /// A surface drawing into \a widget through \a numPasses passes.
    /// Null while DrawDevice::instance() is null, and on failure
    /// (passes exhausted -- every 3D view holds a block too).
    ///
    /// This is the standalone flavour: it owns the widget, and the
    /// caller drives beginFrame/endFrame from paintGL. The other
    /// flavour is attached -- created by the renderer and handed to a
    /// FrameConsumer inside a host frame (Renderer.h,
    /// docs/CAMSimRenderPort.md section 8). Everything below behaves
    /// the same in both, which is the point: a consumer's drawing code
    /// does not know which one it holds.
    static std::unique_ptr<DrawSurface> create(QOpenGLWidget *widget,
                                               unsigned numPasses);

    virtual ~DrawSurface();

    /// True for a surface drawing inside a host frame. On one of
    /// those beginFrame/endFrame are no-ops -- the host owns the
    /// frame boundary -- and the host accessors below describe what
    /// this frame is being drawn into.
    virtual bool attached() const { return false; }

    /// The target a consumer should composite its finished image
    /// into: the host's scene colour+depth when attached, and an
    /// invalid handle otherwise -- which setPassTarget already reads
    /// as "this surface's own backbuffer". So
    ///
    ///     surface.setPassTarget(resolvePass, surface.hostTarget());
    ///
    /// is right in both flavours and needs no test of which one it is.
    virtual TargetHandle hostTarget() const { return {}; }
    /// The colour and depth attachments of hostTarget(), for a
    /// consumer that needs to sample rather than only write them.
    /// Both are the surface's own backbuffer attachments when not
    /// attached.
    virtual TextureHandle hostColor() const { return {}; }
    virtual TextureHandle hostDepth() const { return {}; }
    /// The pixel size of hostTarget(), which is NOT always the
    /// widget's: a host's scene target follows its own effect
    /// resolution and its sub-view banks. Size intermediate targets
    /// to this.
    virtual void hostSize(int &width, int &height) const
    { width = 0; height = 0; }
    /// The camera hostTarget() was drawn with this frame: \a view and
    /// \a proj are column-major 4x4, as in GL. False when there is no
    /// host (a standalone surface), leaving both untouched.
    ///
    /// A consumer needs these to place its own image in the host's
    /// depth: its geometry is in its own view space, and only the
    /// host's view-projection says where that lands in the depth
    /// buffer everything else in the frame shares.
    virtual bool hostCamera(float view[16], float proj[16]) const
    { (void)view; (void)proj; return false; }
    /// True when hostTarget()'s colour holds LINEAR light -- the
    /// colour-managed floating-point scene target, which an output
    /// transform encodes later. A consumer that shades in display
    /// space must decode to linear before writing, or its colour goes
    /// through that transform twice.
    virtual bool hostLinearColor() const { return false; }

    /// The backend's own id of \a pass -- a bgfx view id on that
    /// backend -- for a consumer compiled INSIDE the renderer library
    /// that speaks the backend directly because the facade does not
    /// carry what it needs (the ImGui renderer wants transient
    /// buffers and embedded shaders). Anything outside the library
    /// has no use for it. Valid only between beginFrame and endFrame
    /// on an attached surface, whose ids are the host's and move
    /// every frame; 0xffff when there is no such id.
    virtual uint16_t nativePassId(unsigned pass) const
    { (void)pass; return 0xffff; }

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
