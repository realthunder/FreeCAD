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
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#include <stb/stb_image.h>

#include <algorithm>
#include <cstring>

#include "ImageDecode.h"

namespace Render {

static bool startsWith(const uint8_t *bytes, size_t size, const char *sig)
{
    const size_t n = std::strlen(sig);
    return size >= n && std::memcmp(bytes, sig, n) == 0;
}

bool isEncodedImage(const uint8_t *bytes, size_t size)
{
    if (!bytes || size < 8)
        return false;
    if (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff)
        return true;   // JPEG
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a,
                                   0x0a};
    if (std::equal(png, png + 8, bytes))
        return true;
    // Radiance, the one payload whose pixels are floats. These two
    // signatures and not the looser "#?" the producer's own reader
    // accepts: what this answers yes to is what the decoder below has
    // to be able to read back, and a header line ending any other way
    // is a file that would travel and then not decode.
    return startsWith(bytes, size, "#?RADIANCE\n")
        || startsWith(bytes, size, "#?RGBE\n");
}

/// One 2x2 box step, which is the mip step the GPU upload takes anyway,
/// so a capped picture is the mip the full one would have drawn at that
/// size. Bytes round; floats do not have to.
template <class T>
static T boxAverage(T a, T b, T c, T d);

template <>
uint8_t boxAverage(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return uint8_t((int(a) + int(b) + int(c) + int(d) + 2) / 4);
}

template <>
float boxAverage(float a, float b, float c, float d)
{
    return (a + b + c + d) * 0.25f;
}

/// Halve \a bytes until neither side exceeds \a maxSide (0: leave it).
template <class T>
static void halveDown(std::vector<uint8_t> &bytes, int &width, int &height,
                      int components, int maxSide)
{
    int w = width;
    int h = height;
    while (maxSide > 0 && (w > maxSide || h > maxSide) && (w > 1 || h > 1)) {
        const int nw = std::max(1, w >> 1);
        const int nh = std::max(1, h >> 1);
        std::vector<uint8_t> next(size_t(nw) * nh * components * sizeof(T));
        const T *src = reinterpret_cast<const T *>(bytes.data());
        T *dst = reinterpret_cast<T *>(next.data());
        for (int y = 0; y < nh; ++y) {
            const int y0 = std::min(2 * y, h - 1);
            const int y1 = std::min(2 * y + 1, h - 1);
            for (int x = 0; x < nw; ++x) {
                const int x0 = std::min(2 * x, w - 1);
                const int x1 = std::min(2 * x + 1, w - 1);
                for (int c = 0; c < components; ++c) {
                    dst[(size_t(y) * nw + x) * components + c] =
                        boxAverage<T>(src[(size_t(y0) * w + x0) * components + c],
                                      src[(size_t(y0) * w + x1) * components + c],
                                      src[(size_t(y1) * w + x0) * components + c],
                                      src[(size_t(y1) * w + x1) * components + c]);
                }
            }
        }
        bytes.swap(next);
        w = nw;
        h = nh;
    }
    width = w;
    height = h;
}

bool decodeImage(const uint8_t *bytes, size_t size, int components,
                 int maxSide, int &width, int &height,
                 std::vector<uint8_t> &pixels, bool floatSamples)
{
    if (!bytes || !size || size > size_t(INT32_MAX))
        return false;
    if (components != 1 && components != 3 && components != 4)
        return false;
    int w = 0, h = 0, n = 0;
    // The engine's rows are bottom-up like GL; stb hands the file's top
    // row first. The flag is global state, so it is set every call
    // rather than trusted from the last one. It flips the float path
    // too.
    stbi_set_flip_vertically_on_load(1);
    if (floatSamples) {
        float *px = stbi_loadf_from_memory(bytes, int(size), &w, &h, &n,
                                           components);
        if (!px || w <= 0 || h <= 0) {
            if (px)
                stbi_image_free(px);
            return false;
        }
        const auto *raw = reinterpret_cast<const uint8_t *>(px);
        std::vector<uint8_t> out(
            raw, raw + size_t(w) * h * components * sizeof(float));
        stbi_image_free(px);
        halveDown<float>(out, w, h, components, maxSide);
        width = w;
        height = h;
        pixels.swap(out);
        return true;
    }
    stbi_uc *px = stbi_load_from_memory(bytes, int(size), &w, &h, &n,
                                        components);
    if (!px || w <= 0 || h <= 0) {
        if (px)
            stbi_image_free(px);
        return false;
    }
    std::vector<uint8_t> out(px, px + size_t(w) * h * components);
    stbi_image_free(px);
    halveDown<uint8_t>(out, w, h, components, maxSide);
    width = w;
    height = h;
    pixels.swap(out);
    return true;
}

} // namespace Render
