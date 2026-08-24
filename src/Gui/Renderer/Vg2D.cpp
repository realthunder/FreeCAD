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

#include <cstdio>
#include <map>
#include <vector>

#include <bx/allocator.h>

#include "Vg2D.h"

using namespace Render;

// vg keeps the allocator for the context's whole life.
static bx::DefaultAllocator _allocator;

static std::map<std::string, vg::FontHandle> _fonts;
// Registered font bytes, retained for the process life: registration is
// legal before any context exists (the TechDraw feed records pages
// before the first render), and a context rebuild re-creates every
// handle from these.
static std::map<std::string, std::vector<uint8_t>> _fontData;

Vg2D& Vg2D::instance()
{
    // Leaked on purpose: a function-local static would be destroyed
    // after the renderer has already shut bgfx down, and destroying
    // the vg context then touches a dead device.
    static Vg2D* inst = new Vg2D;
    return *inst;
}

bool Vg2D::init()
{
    if (ctx)
        return true;
    // The caller guarantees an initialized bgfx device: vg::createContext
    // reads bgfx::getCaps() and creates GPU resources.
    vg::ContextConfig cfg;
    cfg.m_MaxGradients = 256;
    // Image patterns are frame-transient: every image op drawn in a
    // frame consumes one slot, so this bounds visible image draws.
    cfg.m_MaxImagePatterns = 256;
    cfg.m_MaxFonts = 8;
    cfg.m_MaxStateStackSize = 32;
    // Template + symbols + view images + bitmap hatch tiles; the slot
    // table is a few dozen bytes per entry, textures are created only
    // for registered images.
    cfg.m_MaxImages = 256;
    // One command list per page item: a real drawing holds tens of
    // thousands of edges. The context allocates the slot table up front
    // at 40 bytes a slot, so the uint16 handle space costs ~2.6MB --
    // taken whole rather than sized down, because a page over the limit
    // does not degrade, it drops items (Page2D counts the drops).
    cfg.m_MaxCommandLists = 65534;
    cfg.m_MaxVBVertices = 65536;
    cfg.m_FontAtlasImageFlags = vg::ImageFlags::Filter_Bilinear;
    cfg.m_MaxCommandListDepth = 16;
    // Page2D owns the bgfx view transform: it splits the page zoom into
    // the vg-side band scale and a residual it applies in the view
    // matrix. vg resetting the transform at end() would overwrite that.
    cfg.m_ResetViewTransformOnEnd = false;
    ctx = vg::createContext(&_allocator, &cfg);
    if (ctx) {
        for (auto& kv : _fontData) {
            vg::FontHandle handle =
                vg::createFont(ctx, kv.first.c_str(), kv.second.data(),
                               (uint32_t)kv.second.size(), 0);
            if (vg::isValid(handle))
                _fonts[kv.first] = handle;
        }
    }
    return ctx != nullptr;
}

void Vg2D::shutdown()
{
    if (!ctx)
        return;
    vg::destroyContext(ctx);
    ctx = nullptr;
    _fonts.clear();
    // Every command list handle out there died with the context.
    ++gen;
}

vg::FontHandle Vg2D::loadFont(const char* name, const void* data, uint32_t size)
{
    if (!name || !data || !size)
        return VG_INVALID_HANDLE;
    if (_fontData.count(name)) {
        auto it = _fonts.find(name);
        return it == _fonts.end() ? vg::FontHandle(VG_INVALID_HANDLE)
                                  : it->second;
    }
    const uint8_t* bytes = (const uint8_t*)data;
    _fontData[name].assign(bytes, bytes + size);
    if (!ctx) // registered; the handle appears when init() runs
        return VG_INVALID_HANDLE;
    // No DontCopyData: vg copies, our retained buffer stays ours.
    vg::FontHandle handle =
        vg::createFont(ctx, name, _fontData[name].data(), size, 0);
    if (vg::isValid(handle))
        _fonts[name] = handle;
    return handle;
}

vg::FontHandle Vg2D::loadFontFile(const char* name, const char* path)
{
    if (!name || !path)
        return VG_INVALID_HANDLE;
    if (_fontData.count(name)) {
        auto it = _fonts.find(name);
        return it == _fonts.end() ? vg::FontHandle(VG_INVALID_HANDLE)
                                  : it->second;
    }
    std::vector<uint8_t> data;
    if (FILE* f = fopen(path, "rb")) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > 0) {
            data.resize((size_t)size);
            if (fread(data.data(), 1, data.size(), f) != data.size())
                data.clear();
        }
        fclose(f);
    }
    if (data.empty())
        return VG_INVALID_HANDLE;
    return loadFont(name, data.data(), (uint32_t)data.size());
}

vg::FontHandle Vg2D::font(const char* name) const
{
    auto it = _fonts.find(name ? name : "");
    return it == _fonts.end() ? vg::FontHandle(VG_INVALID_HANDLE) : it->second;
}
