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

// The one place stb_image is compiled for the renderer, on every tier:
// the streamed-frame decoder of the browser viewer (main.cpp) and the
// texture transport of SceneDump.cpp both read through it. bimg's
// vendored copy, without its decode library, which neither tier links.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb/stb_image.h>

#include <algorithm>

#include "ImageDecode.h"

namespace Render {

bool isEncodedImage(const uint8_t *bytes, size_t size)
{
    if (!bytes || size < 8)
        return false;
    if (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff)
        return true;   // JPEG
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a,
                                   0x0a};
    return std::equal(png, png + 8, bytes);
}

bool decodeImage(const uint8_t *bytes, size_t size, int components,
                 int maxSide, int &width, int &height,
                 std::vector<uint8_t> &pixels)
{
    if (!bytes || !size || size > size_t(INT32_MAX))
        return false;
    if (components != 1 && components != 3 && components != 4)
        return false;
    int w = 0, h = 0, n = 0;
    // The engine's rows are bottom-up like GL; stb hands the file's top
    // row first. The flag is global state, so it is set every call
    // rather than trusted from the last one.
    stbi_set_flip_vertically_on_load(1);
    stbi_uc *px = stbi_load_from_memory(bytes, int(size), &w, &h, &n,
                                        components);
    if (!px || w <= 0 || h <= 0) {
        if (px)
            stbi_image_free(px);
        return false;
    }
    std::vector<uint8_t> out(px, px + size_t(w) * h * components);
    stbi_image_free(px);
    // Halve until it fits: a 2x2 box, which is the mip step the GPU
    // upload takes anyway, so a capped picture is the mip the full one
    // would have drawn at that size.
    while (maxSide > 0 && (w > maxSide || h > maxSide) && (w > 1 || h > 1)) {
        const int nw = std::max(1, w >> 1);
        const int nh = std::max(1, h >> 1);
        std::vector<uint8_t> next(size_t(nw) * nh * components);
        for (int y = 0; y < nh; ++y) {
            const int y0 = std::min(2 * y, h - 1);
            const int y1 = std::min(2 * y + 1, h - 1);
            for (int x = 0; x < nw; ++x) {
                const int x0 = std::min(2 * x, w - 1);
                const int x1 = std::min(2 * x + 1, w - 1);
                for (int c = 0; c < components; ++c) {
                    const int s = out[(size_t(y0) * w + x0) * components + c]
                        + out[(size_t(y0) * w + x1) * components + c]
                        + out[(size_t(y1) * w + x0) * components + c]
                        + out[(size_t(y1) * w + x1) * components + c];
                    next[(size_t(y) * nw + x) * components + c] =
                        uint8_t((s + 2) / 4);
                }
            }
        }
        out.swap(next);
        w = nw;
        h = nh;
    }
    width = w;
    height = h;
    pixels.swap(out);
    return true;
}

} // namespace Render
