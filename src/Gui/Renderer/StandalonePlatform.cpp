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

bgfx::ShaderHandle loadShaderData(const std::string &path)
{
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp)
        return BGFX_INVALID_HANDLE;
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(fp);
        return BGFX_INVALID_HANDLE;
    }
    // One byte over: bgfx wants the block NUL-terminated (the shader
    // name tail is read as a string).
    std::vector<char> data(size_t(size) + 1, '\0');
    size_t read = std::fread(data.data(), 1, size_t(size), fp);
    std::fclose(fp);
    if (read != size_t(size))
        return BGFX_INVALID_HANDLE;
    return bgfx::createShader(bgfx::copy(data.data(),
                                         uint32_t(data.size())));
}

#endif // FC_RENDERER_STANDALONE
