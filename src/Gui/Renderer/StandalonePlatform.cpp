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

#ifdef FC_RENDERER_STANDALONE

#include "StandalonePlatform.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

/// Mirror of bgfx_utils' loadShader: the per-API shader subdirectory of
/// the runtime asset path.
const char *shaderApiDir()
{
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::OpenGL:   return "glsl";
    case bgfx::RendererType::OpenGLES: return "essl";
    case bgfx::RendererType::Vulkan:   return "spirv";
    case bgfx::RendererType::Metal:    return "metal";
    default:                           return "glsl";
    }
}

} // anonymous namespace

bgfx::ShaderHandle loadShader(const bx::StringView &name, const char *path)
{
    std::string file(path ? path : "");
    if (!file.empty() && file.back() != '/')
        file += '/';
    file += "shaders/";
    file += shaderApiDir();
    file += '/';
    file.append(name.getPtr(), name.getLength());
    file += ".bin";

    FILE *fp = std::fopen(file.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "bgfx: cannot open shader %s\n", file.c_str());
        return BGFX_INVALID_HANDLE;
    }
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    const bgfx::Memory *mem = bgfx::alloc(uint32_t(size + 1));
    size_t read = std::fread(mem->data, 1, size_t(size), fp);
    std::fclose(fp);
    mem->data[size] = '\0';
    if (read != size_t(size)) {
        std::fprintf(stderr, "bgfx: short read on shader %s\n", file.c_str());
        return BGFX_INVALID_HANDLE;
    }
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    bgfx::setName(handle, name.getPtr(), name.getLength());
    return handle;
}

bgfx::ProgramHandle loadProgram(const bx::StringView &vsName,
                                const bx::StringView &fsName,
                                const char *path)
{
    bgfx::ShaderHandle vsh = loadShader(vsName, path);
    bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
    if (!fsName.isEmpty())
        fsh = loadShader(fsName, path);
    return bgfx::createProgram(vsh, fsh, true);
}

#endif // FC_RENDERER_STANDALONE
