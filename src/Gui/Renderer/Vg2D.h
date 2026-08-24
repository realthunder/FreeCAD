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
 ****************************************************************************/

#ifndef RENDERER_VG2D_H
#define RENDERER_VG2D_H

/// The vg-renderer context layer of the 2D page engine
/// (docs/TechDrawPortAndSection.md sec 16, milestone M1): one vg::Context
/// per process, created on demand once bgfx has a device, plus the
/// name-keyed font registry shared by every page. Page2D holds the
/// retained page content and drives this context per frame.

#include <cstdint>
#include <string>

#include <vg/vg.h>

#include "Renderer.h"

namespace Render {

class RendererExport Vg2D
{
public:
    /// The process-wide instance. Does not create the vg context; call
    /// init() (idempotent) once bgfx is initialized. Deliberately
    /// leaked: a static-destruction-order teardown would touch bgfx
    /// after the renderer shut it down.
    static Vg2D& instance();

    /// Create the vg context if it does not exist yet. Requires an
    /// initialized bgfx device; returns false without one.
    bool init();

    /// Destroy the vg context and forget the fonts. Must run before
    /// bgfx::shutdown(); safe to call when never initialized.
    void shutdown();

    bool initialized() const { return ctx != nullptr; }

    /// Bumped by every shutdown(): a retained page whose stored
    /// generation differs holds handles into a destroyed context and
    /// must forget them (Page2D does this on render).
    uint32_t generation() const { return gen; }

    vg::Context* context() const { return ctx; }

    /// Register a font under a name the page items refer to. The data
    /// is copied; replacing an existing name is refused (fontstash has
    /// no unload), returning the existing handle.
    vg::FontHandle loadFont(const char* name, const void* data, uint32_t size);

    /// Convenience: read the file and loadFont() it.
    vg::FontHandle loadFontFile(const char* name, const char* path);

    /// The handle registered under name, or an invalid handle.
    vg::FontHandle font(const char* name) const;

private:
    Vg2D() = default;
    ~Vg2D() = default;
    Vg2D(const Vg2D&) = delete;
    Vg2D& operator=(const Vg2D&) = delete;

    vg::Context* ctx = nullptr;
    uint32_t gen = 0;
};

} // namespace Render

#endif // RENDERER_VG2D_H
