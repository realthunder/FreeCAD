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

Vg2D& Vg2D::instance()
{
    static Vg2D inst;
    return inst;
}

Vg2D::~Vg2D()
{
    shutdown();
}

bool Vg2D::init()
{
    if (ctx)
        return true;
    // The caller guarantees an initialized bgfx device: vg::createContext
    // reads bgfx::getCaps() and creates GPU resources.
    vg::ContextConfig cfg;
    cfg.m_MaxGradients = 256;
    cfg.m_MaxImagePatterns = 64;
    cfg.m_MaxFonts = 8;
    cfg.m_MaxStateStackSize = 32;
    cfg.m_MaxImages = 16;
    // One command list per page item: a real drawing holds thousands of
    // edges. The context allocates a slot table up front; a slot is a
    // small struct, so this is a few MB of table, not geometry.
    cfg.m_MaxCommandLists = 16384;
    cfg.m_MaxVBVertices = 65536;
    cfg.m_FontAtlasImageFlags = vg::ImageFlags::Filter_Bilinear;
    cfg.m_MaxCommandListDepth = 16;
    // Page2D owns the bgfx view transform: it splits the page zoom into
    // the vg-side band scale and a residual it applies in the view
    // matrix. vg resetting the transform at end() would overwrite that.
    cfg.m_ResetViewTransformOnEnd = false;
    ctx = vg::createContext(&_allocator, &cfg);
    return ctx != nullptr;
}

void Vg2D::shutdown()
{
    if (!ctx)
        return;
    vg::destroyContext(ctx);
    ctx = nullptr;
    _fonts.clear();
}

vg::FontHandle Vg2D::loadFont(const char* name, const void* data, uint32_t size)
{
    if (!ctx || !name || !data || !size)
        return VG_INVALID_HANDLE;
    auto it = _fonts.find(name);
    if (it != _fonts.end())
        return it->second;
    // No DontCopyData: vg copies, the caller's buffer can go away.
    vg::FontHandle handle = vg::createFont(
        ctx, name, (uint8_t*)const_cast<void*>(data), size, 0);
    if (vg::isValid(handle))
        _fonts[name] = handle;
    return handle;
}

vg::FontHandle Vg2D::loadFontFile(const char* name, const char* path)
{
    if (!ctx || !path)
        return VG_INVALID_HANDLE;
    auto it = _fonts.find(name ? name : "");
    if (it != _fonts.end())
        return it->second;
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
