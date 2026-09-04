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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

#include "CyclesSceneP.h"
#include "Environment.h"
#ifdef HAVE_MATERIALX
#include "CyclesMaterialXP.h"
#endif

#include <Base/Console.h>

#include "kernel/types.h"
#include "scene/attribute.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/image.h"
#include "scene/image_loader.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/colorspace.h"
#include "util/half.h"
#include "util/image_metadata.h"
#include "util/transform.h"

namespace Render::Cycles {

namespace {

constexpr float kPi = 3.14159265358979323846f;

/// 0xRRGGBBAA authored colour to linear rgb + coverage alpha, the C++
/// twin of the bgfx path's unpackAuthoredColor: decoded when the
/// pipeline is colour managed, taken raw when it is not (then the two
/// errors cancel the way they always did). Alpha is never decoded.
void unpackAuthored(uint32_t rgba, float out[4], bool managed)
{
    out[0] = ((rgba >> 24) & 0xff) / 255.0f;
    out[1] = ((rgba >> 16) & 0xff) / 255.0f;
    out[2] = ((rgba >> 8) & 0xff) / 255.0f;
    out[3] = (rgba & 0xff) / 255.0f;
    if (managed)
        for (int i = 0; i < 3; ++i)
            out[i] = srgbToLinear(out[i]);
}

/// Same for the rgb(a)8 byte streams (vertex colours, the per-face
/// material stream), which fc_color.sh decodes on the GPU.
void unpackBytes(const uint8_t *p, int n, float out[4], bool managed)
{
    for (int i = 0; i < 4; ++i)
        out[i] = i < n ? p[i] / 255.0f : 1.0f;
    if (managed)
        for (int i = 0; i < 3; ++i)
            out[i] = srgbToLinear(out[i]);
}

float perceivedBrightness(const float c[3])
{
    return std::sqrt(0.299f * c[0] * c[0] + 0.587f * c[1] * c[1] + 0.114f * c[2] * c[2]);
}

/// Khronos' specular-glossiness to metallic-roughness solve, the C++
/// twin of fcBaseFromSpecular in fc_mesh_lighting.sh: the metalness
/// for which a dielectric f0 of 0.04 and a base colour reproduce the
/// Phong diffuse/specular pair, then the base recombined from both
/// readings, trusting the specular one as the surface turns metallic.
void baseFromSpecular(const float diffuse[3], const float spec[3], float base[3], float &metal)
{
    const float dielectric = 0.04f;
    const float oneMinusSS = 1.0f - std::max({spec[0], spec[1], spec[2]});
    const float ds = perceivedBrightness(diffuse);
    const float ss = perceivedBrightness(spec);
    const float b = ds * oneMinusSS / (1.0f - dielectric) + ss - 2.0f * dielectric;
    const float c = dielectric - ss;
    const float disc = std::max(b * b - 4.0f * dielectric * c, 0.0f);
    metal = ss < dielectric
        ? 0.0f
        : std::clamp((-b + std::sqrt(disc)) / (2.0f * dielectric), 0.0f, 1.0f);
    const float wd = oneMinusSS / (1.0f - dielectric) / std::max(1.0f - metal, 1.0e-4f);
    const float m2 = metal * metal;
    for (int i = 0; i < 3; ++i) {
        const float fromDiffuse = diffuse[i] * wd;
        const float fromSpec = (spec[i] - dielectric * (1.0f - metal)) / std::max(metal, 1.0e-4f);
        base[i] = std::clamp(fromDiffuse + (fromSpec - fromDiffuse) * m2, 0.0f, 1.0f);
    }
}

/// Phong shininess (0..1) to GGX roughness, as BGFXView::
/// setTriangleFrameState does it: the exponent the frame's mapping
/// reads out of the shininess, then the fourth root of 2 / (n + 2).
float roughnessFromShininess(float shininess, int mapping)
{
    shininess = std::clamp(shininess, 0.0f, 1.0f);
    float exponent;
    if (mapping == 1) {
        const float denom = std::max(1.0f - shininess, 1.0e-4f);
        exponent = 128.0f * shininess / denom;
    }
    else
        exponent = shininess * 128.0f;
    return std::pow(2.0f / (exponent + 2.0f), 0.25f);
}

/// GL-layout (column-major) 4x4 to Cycles' 3x4 affine.
ccl::Transform toTransform(const float m[16])
{
    ccl::Transform t;
    t.x = ccl::make_float4(m[0], m[4], m[8], m[12]);
    t.y = ccl::make_float4(m[1], m[5], m[9], m[13]);
    t.z = ccl::make_float4(m[2], m[6], m[10], m[14]);
    return t;
}

/// An affine frame whose z axis is \a z (unit), for placing a light.
ccl::Transform frameAlongZ(const float z[3], const float origin[3])
{
    ccl::float3 az = ccl::normalize(ccl::make_float3(z[0], z[1], z[2]));
    ccl::float3 helper = std::fabs(az.z) < 0.9f ? ccl::make_float3(0.0f, 0.0f, 1.0f)
                                                : ccl::make_float3(1.0f, 0.0f, 0.0f);
    ccl::float3 ax = ccl::normalize(ccl::cross(helper, az));
    ccl::float3 ay = ccl::cross(az, ax);
    ccl::Transform t;
    t.x = ccl::make_float4(ax.x, ay.x, az.x, origin[0]);
    t.y = ccl::make_float4(ax.y, ay.y, az.y, origin[1]);
    t.z = ccl::make_float4(ax.z, ay.z, az.z, origin[2]);
    return t;
}

/// The cap fill colour of a section, as the GL renderer and the bgfx
/// backend compute it: the complement, with the muddy middle pushed
/// to a readable grey and near-black lifted off the floor. Alpha is
/// the material's.
uint32_t invertCapColor(uint32_t col)
{
    auto inv = [](uint32_t c) -> uint32_t { return (c > 120 && c < 140) ? 180 : 255 - c; };
    uint32_t r = inv((col >> 24) & 0xff);
    uint32_t g = inv((col >> 16) & 0xff);
    uint32_t b = inv((col >> 8) & 0xff);
    if (r + g + b < 10)
        r = g = b = 50;
    return (r << 24) | (g << 16) | (b << 8) | (col & 0xff);
}

/// One filled cross-section: the triangles of \a mesh in [start,
/// start + count) cut by \a plane (in the MESH's own space, the
/// caller having pulled it back through the model transform), filled,
/// and returned as local-space triangle corners -- 9 floats each.
///
/// Every triangle that crosses the plane contributes one segment, and
/// those segments are the closed boundary of the cut region. The fill
/// is a trapezoidal sweep in the plane's own 2D frame: sort the
/// segment endpoints by height, and between two consecutive heights
/// no segment begins or ends, so each span across that band is
/// bounded by two straight edges and the quad between them IS the
/// region -- exact, not a stair-step. Pairing the crossings even-odd
/// is what fills a section with a hole in it (a tube, a bore)
/// correctly, and it asks nothing of the mesh's winding, which is
/// why this needs no loop chaining and no ear clipping.
///
/// The cut face points AWAY from the kept half (the solid is on the
/// plane's positive side), so the triangles are wound for a geometric
/// normal of -n.
std::vector<float> buildCapTriangles(const MeshData &mesh,
                                     int start,
                                     int count,
                                     const float plane[4])
{
    std::vector<float> out;
    const float len = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
    if (len < 1.0e-12f)
        return out;
    const float N[3] = {plane[0] / len, plane[1] / len, plane[2] / len};
    const float W = plane[3] / len;

    // The plane's own frame: a foot point, and u x v = N.
    float helper[3] = {0.0f, 0.0f, 1.0f};
    if (std::fabs(N[2]) >= 0.9f) {
        helper[0] = 1.0f;
        helper[2] = 0.0f;
    }
    float u[3] = {helper[1] * N[2] - helper[2] * N[1],
                  helper[2] * N[0] - helper[0] * N[2],
                  helper[0] * N[1] - helper[1] * N[0]};
    const float ulen = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ulen < 1.0e-12f)
        return out;
    for (float &c : u)
        c /= ulen;
    const float v[3] = {N[1] * u[2] - N[2] * u[1],
                        N[2] * u[0] - N[0] * u[2],
                        N[0] * u[1] - N[1] * u[0]};
    const float origin[3] = {-W * N[0], -W * N[1], -W * N[2]};

    // The boundary segments, in the plane's 2D frame.
    struct Seg {
        float x0, y0, x1, y1;  // y0 <= y1
    };
    std::vector<Seg> segs;
    const int32_t *idx = mesh.triangleIndices + start;
    for (int t = 0; t + 2 < count; t += 3) {
        float p[3][3];
        float d[3];
        bool bad = false;
        for (int j = 0; j < 3; ++j) {
            const int32_t vi = idx[t + j];
            if (vi < 0 || vi >= mesh.numVertices) {
                bad = true;
                break;
            }
            const float *src = mesh.positions + size_t(vi) * 3;
            std::copy(src, src + 3, p[j]);
            d[j] = N[0] * p[j][0] + N[1] * p[j][1] + N[2] * p[j][2] + W;
        }
        if (bad)
            continue;
        float pts[2][2];
        int n = 0;
        for (int e = 0; e < 3 && n < 3; ++e) {
            const int a = e;
            const int b = (e + 1) % 3;
            // A vertex exactly on the plane counts as kept, so a
            // triangle yields either no crossing or exactly two.
            if ((d[a] < 0.0f) == (d[b] < 0.0f))
                continue;
            const float f = d[a] / (d[a] - d[b]);
            float q[3];
            for (int k = 0; k < 3; ++k)
                q[k] = p[a][k] + (p[b][k] - p[a][k]) * f - origin[k];
            if (n < 2) {
                pts[n][0] = q[0] * u[0] + q[1] * u[1] + q[2] * u[2];
                pts[n][1] = q[0] * v[0] + q[1] * v[1] + q[2] * v[2];
            }
            ++n;
        }
        if (n != 2)
            continue;
        Seg seg;
        if (pts[0][1] <= pts[1][1])
            seg = Seg{pts[0][0], pts[0][1], pts[1][0], pts[1][1]};
        else
            seg = Seg{pts[1][0], pts[1][1], pts[0][0], pts[0][1]};
        if (seg.y1 - seg.y0 > 0.0f)
            segs.push_back(seg);
    }
    if (segs.empty())
        return out;

    // Sweep the bands. Sorting the segments by their lower end lets
    // the active set be maintained instead of rescanned, which keeps
    // a section of a million-triangle solid linear in its segments.
    std::vector<float> heights;
    heights.reserve(segs.size() * 2);
    for (const Seg &sg : segs) {
        heights.push_back(sg.y0);
        heights.push_back(sg.y1);
    }
    std::sort(heights.begin(), heights.end());
    heights.erase(std::unique(heights.begin(), heights.end()), heights.end());
    std::vector<const Seg *> order;
    order.reserve(segs.size());
    for (const Seg &sg : segs)
        order.push_back(&sg);
    std::sort(order.begin(), order.end(),
              [](const Seg *a, const Seg *b) { return a->y0 < b->y0; });

    auto xAt = [](const Seg &sg, float y) {
        return sg.x0 + (sg.x1 - sg.x0) * (y - sg.y0) / (sg.y1 - sg.y0);
    };
    auto emit = [&](float x, float y) {
        for (int k = 0; k < 3; ++k)
            out.push_back(origin[k] + x * u[k] + y * v[k]);
    };

    std::vector<const Seg *> active;
    std::vector<std::pair<float, const Seg *>> crossings;
    size_t next = 0;
    for (size_t band = 0; band + 1 < heights.size(); ++band) {
        const float lo = heights[band];
        const float hi = heights[band + 1];
        if (hi - lo <= 0.0f)
            continue;
        while (next < order.size() && order[next]->y0 <= lo)
            active.push_back(order[next++]);
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [lo](const Seg *sg) { return sg->y1 <= lo; }),
                     active.end());
        const float mid = 0.5f * (lo + hi);
        crossings.clear();
        for (const Seg *sg : active) {
            if (sg->y0 > mid || sg->y1 < mid)
                continue;
            crossings.emplace_back(xAt(*sg, mid), sg);
        }
        std::sort(crossings.begin(), crossings.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        // Even-odd: between the first and second crossing is inside,
        // between the second and third outside, and so on -- which is
        // what makes a bore in the section come out empty.
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const Seg &a = *crossings[i].second;
            const Seg &b = *crossings[i + 1].second;
            const float axl = xAt(a, lo);
            const float axh = xAt(a, hi);
            const float bxl = xAt(b, lo);
            const float bxh = xAt(b, hi);
            if (bxl - axl <= 0.0f && bxh - axh <= 0.0f)
                continue;
            // Wound for a -N geometric normal: the cut face looks out
            // of the solid, which sits on the plane's positive side.
            emit(axl, lo);
            emit(bxh, hi);
            emit(bxl, lo);
            emit(axl, lo);
            emit(axh, hi);
            emit(bxh, hi);
        }
    }
    return out;
}

/// Bake the environment to an equirectangular float picture. Baked,
/// rather than described, because the procedural presets are C++
/// functions of direction (Environment.h) with no Cycles node
/// equivalent, and baking a user picture through the same sampler
/// keeps both the sphere-map convention and the sRGB decode in one
/// place.
///
/// Layout follows the kernel's direction_to_equirectangular: column u
/// = 0.5 - atan2(y, x) / 2pi, row v = 1 - acos(z) / pi, row 0 at the
/// nadir.
std::vector<float> bakeEnvironment(const PBRConfig &pbr, bool managed,
                                   int width, int height)
{
    std::vector<float> rgba(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) {
        const float v = (y + 0.5f) / height;
        const float theta = (1.0f - v) * kPi;
        const float st = std::sin(theta);
        const float ct = std::cos(theta);
        for (int x = 0; x < width; ++x) {
            const float u = (x + 0.5f) / width;
            const float phi = (0.5f - u) * 2.0f * kPi;
            const float d[3] = {st * std::cos(phi), st * std::sin(phi), ct};
            float *px = rgba.data() + (size_t(y) * width + x) * 4;
            px[0] = px[1] = px[2] = 0.0f;
            envRadiance(pbr, d, px, managed);
            px[3] = 1.0f;
        }
    }
    return rgba;
}

/// Area-average a baked equirect down to \a outWidth x \a outHeight,
/// which is how the background blur is made: a smaller picture read
/// back through the engine's linear interpolation IS the defocused
/// one, the same way the raster background reads a level of its
/// cubemap. Each output texel averages the block of input texels it
/// covers, so nothing is dropped -- a point source stays as bright as
/// its share of the block, which is what keeps a blurred sky from
/// losing its sun.
std::vector<float> downsampleEquirect(const std::vector<float> &rgba,
                                      int width, int height,
                                      int outWidth, int outHeight)
{
    std::vector<float> out(size_t(outWidth) * size_t(outHeight) * 4);
    for (int y = 0; y < outHeight; ++y) {
        const int y0 = y * height / outHeight;
        const int y1 = std::max((y + 1) * height / outHeight, y0 + 1);
        for (int x = 0; x < outWidth; ++x) {
            const int x0 = x * width / outWidth;
            const int x1 = std::max((x + 1) * width / outWidth, x0 + 1);
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const float *px =
                        rgba.data() + (size_t(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c)
                        acc[c] += px[c];
                }
            }
            const float n = float((y1 - y0) * (x1 - x0));
            float *px = out.data() + (size_t(y) * outWidth + x) * 4;
            for (int c = 0; c < 4; ++c)
                px[c] = acc[c] / n;
        }
    }
    return out;
}

/// The width the camera-ray copy of the environment is baked down to
/// for \a blur, or 0 for "no blurred copy" -- which is what zero asks
/// for, and what keeps a sharp world the graph it has always had.
///
/// Eight halvings end to end, the same span the raster background
/// slides along its cubemap's mip chain, so the slider reads as one
/// softness in both. It is measured from the picture this engine bakes
/// rather than from a fixed reference, so zero stays "as sharp as this
/// engine gets": for the procedural presets the two bakes are the same
/// angular resolution and the two backdrops match outright, and for a
/// user picture large enough to be baked sharper than the raster
/// cubemap, Cycles keeps that sharpness instead of being blurred down
/// to meet it.
int envBlurWidth(float blur, int width)
{
    blur = std::clamp(blur, 0.0f, 1.0f);
    if (blur <= 0.0f)
        return 0;
    const int out = int(std::lround(double(width)
                                    * std::pow(2.0, -8.0 * double(blur))));
    return std::clamp(out, 4, width - 1);
}

/// A baked equirect the engine's image manager serves to the world
/// shader.
class BakedEnvironment : public ccl::ImageLoader
{
public:
    BakedEnvironment(std::vector<float> pixels, int width, int height)
        : width(width)
        , height(height)
        , rgba(std::move(pixels))
    {}

    bool load_metadata(ccl::ImageMetaData &metadata,
                       const ccl::ImageLoaderParams & /*params*/,
                       ccl::Progress & /*progress*/) override
    {
        metadata.width = width;
        metadata.height = height;
        metadata.channels = 4;
        metadata.type = ccl::IMAGE_DATA_TYPE_FLOAT4;
        metadata.colorspace = ccl::u_colorspace_scene_linear;
        metadata.is_compressible_as_srgb = false;
        return true;
    }

    bool load_pixels(const ccl::ImageMetaData & /*metadata*/, void *pixels) override
    {
        std::memcpy(pixels, rgba.data(), rgba.size() * sizeof(float));
        return true;
    }

    ccl::string name() const override
    {
        return "FreeCAD environment";
    }

    bool equals(const ccl::ImageLoader &other) const override
    {
        return this == &other;
    }

private:
    int width;
    int height;
    std::vector<float> rgba;
};

/// A TextureImage as Cycles reads it: the pixels the render cache holds,
/// expanded to four channels (Cycles stores byte and float images as one
/// or four channels, never two or three), kept alive by the shared
/// pointer for as long as the image manager may ask for them. The rows
/// are bottom-up like GL, which is the row order Cycles' own loaders
/// hand it (a UV of 0 is the first row). A PICTURE -- the base colour,
/// the emissive map -- is display referred and is declared
/// `scene_linear_srgb` when the pipeline is colour managed: the bytes
/// stay bytes and the kernel decodes them per sample, exactly as
/// fc_mesh_fs.sh does (Blender's path for an 8-bit PNG). NOT
/// `u_colorspace_srgb`: that one is a space to CONVERT FROM, and
/// ImageMetaData::finalize() promotes such an image to half floats for
/// the conversion -- so the buffer handed to load_pixels is not the
/// type load_metadata declared, which is why load_pixels writes the
/// type it is ASKED for. A DATA map -- bump, normal, metallic-roughness
/// -- is declared data and is never decoded, and a float picture is
/// linear radiance already (TextureImage::Sample). Alpha is coverage
/// and is never touched (channel packed).
class PixelImage : public ccl::ImageLoader
{
public:
    PixelImage(std::shared_ptr<const TextureImage> image, ccl::ustring colorspace)
        : image(std::move(image))
        , colorspace(colorspace)
    {}

    bool load_metadata(ccl::ImageMetaData &metadata,
                       const ccl::ImageLoaderParams & /*params*/,
                       ccl::Progress & /*progress*/) override
    {
        if (image->width <= 0 || image->height <= 0 || image->numComponents < 1
            || image->numComponents > 4)
            return false;
        metadata.width = image->width;
        metadata.height = image->height;
        metadata.channels = 4;
        metadata.type = image->sample == TextureImage::F32 ? ccl::IMAGE_DATA_TYPE_FLOAT4
                                                           : ccl::IMAGE_DATA_TYPE_BYTE4;
        metadata.colorspace = colorspace;
        metadata.is_compressible_as_srgb = false;
        return true;
    }

    bool load_pixels(const ccl::ImageMetaData &metadata, void *pixels) override
    {
        const int n = image->numComponents;
        const size_t count = size_t(image->width) * size_t(image->height);
        const size_t bytes = image->sampleSize();
        if (image->pixels.size() < count * size_t(n) * bytes)
            return false;
        // Luminance spreads to rgb, a missing alpha is opaque; written
        // in the type the (finalized) metadata asks for, whatever
        // load_metadata declared.
        const uint8_t *in = image->pixels.data();
        const bool isFloat = image->sample == TextureImage::F32;
        auto texel = [&](size_t i, float q[4]) {
            if (isFloat) {
                float px[4];
                std::memcpy(px, in + i * size_t(n) * 4u, size_t(n) * 4u);
                q[0] = px[0];
                q[1] = n >= 3 ? px[1] : px[0];
                q[2] = n >= 3 ? px[2] : px[0];
                q[3] = n == 2 ? px[1] : n == 4 ? px[3] : 1.0f;
            }
            else {
                const uint8_t *p = in + i * size_t(n);
                q[0] = p[0] / 255.0f;
                q[1] = (n >= 3 ? p[1] : p[0]) / 255.0f;
                q[2] = (n >= 3 ? p[2] : p[0]) / 255.0f;
                q[3] = (n == 2 ? p[1] : n == 4 ? p[3] : 255) / 255.0f;
            }
        };
        switch (metadata.type) {
        case ccl::IMAGE_DATA_TYPE_BYTE4: {
            auto *out = static_cast<uint8_t *>(pixels);
            float q[4];
            for (size_t i = 0; i < count; ++i) {
                texel(i, q);
                for (int c = 0; c < 4; ++c)
                    out[i * 4 + c] = uint8_t(std::lround(std::clamp(q[c], 0.0f, 1.0f) * 255.0f));
            }
            return true;
        }
        case ccl::IMAGE_DATA_TYPE_HALF4: {
            auto *out = static_cast<ccl::half *>(pixels);
            float q[4];
            for (size_t i = 0; i < count; ++i) {
                texel(i, q);
                for (int c = 0; c < 4; ++c)
                    out[i * 4 + c] = ccl::float_to_half_image(q[c]);
            }
            return true;
        }
        case ccl::IMAGE_DATA_TYPE_FLOAT4: {
            auto *out = static_cast<float *>(pixels);
            for (size_t i = 0; i < count; ++i)
                texel(i, out + i * 4);
            return true;
        }
        default:
            return false;
        }
    }

    ccl::string name() const override
    {
        return "FreeCAD texture";
    }

    /// The image manager uploads one copy per distinct loader, and
    /// distinct is this: the same picture declared in the same space.
    bool equals(const ccl::ImageLoader &other) const override
    {
        const auto *o = dynamic_cast<const PixelImage *>(&other);
        return o && o->image->textureId == image->textureId && o->colorspace == colorspace;
    }

private:
    std::shared_ptr<const TextureImage> image;
    ccl::ustring colorspace;
};

/// A baked table -- the finish's groove profile, its noise tiles -- as
/// a data image the graph samples: float texels, one or four channels,
/// never decoded, wrapped so the table is periodic by construction.
/// The tables are process-wide statics (finishProfile and the two
/// below), so equality is identity of the data and the image manager
/// uploads each once.
class TableImage : public ccl::ImageLoader
{
public:
    TableImage(std::shared_ptr<const std::vector<float>> data,
               int width,
               int height,
               int channels,
               const char *label)
        : data(std::move(data))
        , width(width)
        , height(height)
        , channels(channels)
        , label(label)
    {}

    bool load_metadata(ccl::ImageMetaData &metadata,
                       const ccl::ImageLoaderParams & /*params*/,
                       ccl::Progress & /*progress*/) override
    {
        metadata.width = width;
        metadata.height = height;
        metadata.channels = channels;
        metadata.type = channels == 1 ? ccl::IMAGE_DATA_TYPE_FLOAT : ccl::IMAGE_DATA_TYPE_FLOAT4;
        metadata.colorspace = ccl::u_colorspace_data;
        metadata.is_compressible_as_srgb = false;
        return true;
    }

    bool load_pixels(const ccl::ImageMetaData &metadata, void *pixels) override
    {
        const size_t count = size_t(width) * size_t(height) * size_t(channels);
        if (data->size() < count || metadata.channels != channels)
            return false;
        std::memcpy(pixels, data->data(), count * sizeof(float));
        return true;
    }

    ccl::string name() const override
    {
        return label;
    }

    bool equals(const ccl::ImageLoader &other) const override
    {
        const auto *o = dynamic_cast<const TableImage *>(&other);
        return o && o->data == data;
    }

private:
    std::shared_ptr<const std::vector<float>> data;
    int width;
    int height;
    int channels;
    const char *label;
};

/// The finish tables, baked once per process from the same arithmetic
/// fc_finish.sh evaluates per fragment. The raster shader computes the
/// GRADIENT of a height field analytically; Cycles' Bump node wants
/// the HEIGHT and differentiates it, so the profile is integrated
/// here, once, and the lattices of the value noises are laid out as
/// periodic tiles (the same hash on the cell index, wrapped at the
/// tile) that the image's repeat extension continues forever.
constexpr int kProfileSamples = 1024;  ///< texels per groove period
constexpr int kNoise1Cells = 256;      ///< lattice cells per 1-D tile
constexpr int kNoise1Sub = 32;         ///< texels per cell
constexpr int kNoise2Cells = 64;       ///< lattice cells per 2-D tile side
constexpr int kNoise2Sub = 8;          ///< texels per cell
constexpr float kTwoPi = 6.2831853f;

/// fcFinishHash: the value-noise lattice, in float32 as the shader
/// has it (statistically the same lattice; nothing depends on the
/// two agreeing bit for bit, the GPU's sin does not either).
float finishHash(float x, float y)
{
    const float s = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return s - std::floor(s);
}

/// One groove period of the height field, in units of the depth:
/// pi times the integral of fcFinishGroove's normalized flank profile
/// sign(sin) |sin|^0.45 over one period, so that depth * table(x /
/// pitch) has the slope pi * depth / pitch * profile the shader
/// states. Centred on zero (only its differences matter).
std::shared_ptr<const std::vector<float>> finishProfile()
{
    static const std::shared_ptr<const std::vector<float>> table = [] {
        auto t = std::make_shared<std::vector<float>>(kProfileSamples);
        constexpr int sub = 64;
        const double du = 1.0 / (double(kProfileSamples) * sub);
        double acc = 0.0;
        double mean = 0.0;
        for (int i = 0; i < kProfileSamples; ++i) {
            for (int k = 0; k < sub; ++k) {
                const double u = (double(i) * sub + k + 0.5) * du;
                const double sn = std::sin(u * 2.0 * kPi);
                const double s = (sn < 0.0 ? -1.0 : 1.0) * std::pow(std::fabs(sn), 0.45);
                acc += s * du;
                if (k == sub / 2 - 1)
                    (*t)[size_t(i)] = float(acc * kPi);
            }
            mean += (*t)[size_t(i)];
        }
        mean /= kProfileSamples;
        for (float &v : *t)
            v -= float(mean);
        return t;
    }();
    return table;
}

/// The 1-D value noises of the brushed finish, one lane per channel
/// (fcFinishNoise1's lanes 0, 11 and 3: the scratch widths at two
/// scales, and the slow fade along the lay), smoothstep-interpolated
/// between the lattice values and centred as the shader has them.
std::shared_ptr<const std::vector<float>> finishNoise1()
{
    static const std::shared_ptr<const std::vector<float>> table = [] {
        constexpr int width = kNoise1Cells * kNoise1Sub;
        auto t = std::make_shared<std::vector<float>>(size_t(width) * 4);
        const float lanes[3] = {0.0f, 11.0f, 3.0f};
        for (int x = 0; x < width; ++x) {
            const float xc = (x + 0.5f) / kNoise1Sub;
            const int i = int(std::floor(xc));
            const float f = xc - float(i);
            const float u = f * f * (3.0f - 2.0f * f);
            float *px = t->data() + size_t(x) * 4;
            for (int c = 0; c < 3; ++c) {
                const float a = finishHash(float(i % kNoise1Cells), lanes[c]);
                const float b = finishHash(float((i + 1) % kNoise1Cells), lanes[c]);
                px[c] = a + (b - a) * u - 0.5f;
            }
            px[3] = 1.0f;
        }
        return t;
    }();
    return table;
}

/// The 2-D value noise of the blasted finish (fcFinishNoise2), one
/// periodic tile.
std::shared_ptr<const std::vector<float>> finishNoise2()
{
    static const std::shared_ptr<const std::vector<float>> table = [] {
        constexpr int side = kNoise2Cells * kNoise2Sub;
        auto t = std::make_shared<std::vector<float>>(size_t(side) * side);
        for (int y = 0; y < side; ++y) {
            const float yc = (y + 0.5f) / kNoise2Sub;
            const int j = int(std::floor(yc));
            const float fy = yc - float(j);
            const float uy = fy * fy * (3.0f - 2.0f * fy);
            const float j0 = float(j % kNoise2Cells);
            const float j1 = float((j + 1) % kNoise2Cells);
            for (int x = 0; x < side; ++x) {
                const float xc = (x + 0.5f) / kNoise2Sub;
                const int i = int(std::floor(xc));
                const float fx = xc - float(i);
                const float ux = fx * fx * (3.0f - 2.0f * fx);
                const float i0 = float(i % kNoise2Cells);
                const float i1 = float((i + 1) % kNoise2Cells);
                const float a = finishHash(i0, j0);
                const float b = finishHash(i1, j0);
                const float c = finishHash(i0, j1);
                const float d = finishHash(i1, j1);
                const float k1 = b - a;
                const float k2 = c - a;
                const float k3 = a - b - c + d;
                (*t)[size_t(y) * side + x] = a + k1 * ux + k2 * uy + k3 * ux * uy - 0.5f;
            }
        }
        return t;
    }();
    return table;
}

/// The node vocabulary the graph builders share (applyFinish,
/// applyFaceImage): a node per arithmetic step, a null operand standing
/// for the constant beside it. Cycles folds a constant subexpression at
/// graph build, so stating one as a node costs nothing at render.
struct GraphOps {
    using Out = ccl::ShaderOutput *;
    ccl::ShaderGraph *graph;

    Out math(ccl::NodeMathType type, Out a, Out b, float ca = 0.0f, float cb = 0.0f) const
    {
        auto *n = graph->create_node<ccl::MathNode>();
        n->set_math_type(type);
        if (a)
            graph->connect(a, n->input("Value1"));
        else
            n->set_value1(ca);
        if (b)
            graph->connect(b, n->input("Value2"));
        else
            n->set_value2(cb);
        return n->output("Value");
    }
    Out mul(Out a, float c) const
    {
        return math(ccl::NODE_MATH_MULTIPLY, a, nullptr, 0.0f, c);
    }
    Out add(Out a, float c) const
    {
        return math(ccl::NODE_MATH_ADD, a, nullptr, 0.0f, c);
    }
    /// a * b + c, c a link.
    Out madd(Out a, float b, Out c) const
    {
        auto *n = graph->create_node<ccl::MathNode>();
        n->set_math_type(ccl::NODE_MATH_MULTIPLY_ADD);
        graph->connect(a, n->input("Value1"));
        n->set_value2(b);
        graph->connect(c, n->input("Value3"));
        return n->output("Value");
    }
    /// a op c, c a constant vector (null for a unary op).
    ccl::VectorMathNode *vec(ccl::NodeVectorMathType type, Out a, const float c[3]) const
    {
        auto *n = graph->create_node<ccl::VectorMathNode>();
        n->set_math_type(type);
        graph->connect(a, n->input("Vector1"));
        if (c)
            n->set_vector2(ccl::make_float3(c[0], c[1], c[2]));
        return n;
    }
    /// a op b, both links.
    Out vec2(ccl::NodeVectorMathType type, Out a, Out b) const
    {
        auto *n = graph->create_node<ccl::VectorMathNode>();
        n->set_math_type(type);
        graph->connect(a, n->input("Vector1"));
        graph->connect(b, n->input("Vector2"));
        return n->output("Vector");
    }
    Out dot(Out a, const float c[3]) const
    {
        return vec(ccl::NODE_VECTOR_MATH_DOT_PRODUCT, a, c)->output("Value");
    }
    Out combine(Out x, Out y, float cy) const
    {
        auto *n = graph->create_node<ccl::CombineXYZNode>();
        graph->connect(x, n->input("X"));
        if (y)
            graph->connect(y, n->input("Y"));
        else
            n->set_y(cy);
        return n->output("Vector");
    }
    ccl::SeparateXYZNode *separate(Out v) const
    {
        auto *n = graph->create_node<ccl::SeparateXYZNode>();
        graph->connect(v, n->input("Vector"));
        return n;
    }
};

}  // namespace

SceneTranslator::SceneTranslator(ccl::Scene *scene, bool colorManaged)
    : scene(scene)
    , managed(colorManaged)
{}

bool SceneTranslator::translate(const SceneInput &input, RenderReport &report)
{
    // A changed debug view keys every shader afresh (below), which
    // re-keys every mesh, which is the rebuild it is.
    debugView = input.debugView;
    bool changed = translateCamera(input.camera);
    changed |= translateWorld(input.pbr, input.output);

    // Every object of the previous restate is spare until a draw of
    // this one claims it; the draws are walked in order, so two draws
    // under one key (two parts of one object on one mesh) take the
    // objects two-by-two the way they were placed.
    Spare spare;
    for (const Instance &inst : instances)
        spare[inst.key].push_back(inst.object);
    instances.clear();

    float sceneMin[3] = {0.0f, 0.0f, 0.0f};
    float sceneMax[3] = {-1.0f, -1.0f, -1.0f};
    report.skipped = 0;
    report.added = report.removed = report.restated = report.built = report.released = 0;
    for (const DrawCall &draw : input.draws) {
        if (!translateDraw(draw, input.pbr, input.bump, input.section, spare, report, changed)) {
            ++report.skipped;
            continue;
        }
        if (draw.bboxMin[0] > draw.bboxMax[0])
            continue;
        if (sceneMin[0] > sceneMax[0]) {
            std::copy(draw.bboxMin, draw.bboxMin + 3, sceneMin);
            std::copy(draw.bboxMax, draw.bboxMax + 3, sceneMax);
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            sceneMin[i] = std::min(sceneMin[i], draw.bboxMin[i]);
            sceneMax[i] = std::max(sceneMax[i], draw.bboxMax[i]);
        }
    }

    // What no draw claimed is gone: the objects first, then the meshes
    // nothing references any more (an object reads its geometry on
    // the way out, so that order is the only safe one).
    std::set<ccl::Object *> gone;
    for (auto &entry : spare)
        gone.insert(entry.second.begin(), entry.second.end());
    if (!gone.empty()) {
        scene->delete_nodes(gone);
        report.removed = int(gone.size());
        changed = true;
    }
    std::set<ccl::Mesh *> referenced;
    for (const Instance &inst : instances)
        referenced.insert(inst.mesh);
    std::set<ccl::Geometry *> unused;
    for (auto it = meshes.begin(); it != meshes.end();) {
        ccl::Mesh *mesh = it->second.mesh;
        if (referenced.count(mesh)) {
            ++it;
            continue;
        }
        unused.insert(mesh);
        it = meshes.erase(it);
    }
    if (!unused.empty()) {
        scene->delete_nodes(unused);
        report.released = int(unused.size());
        changed = true;
    }
    changed |= releaseUnusedShaders();

    changed |= translateLight(input.light, sceneMin, sceneMax);

    report.meshes = int(meshes.size());
    report.objects = int(instances.size());
    report.shaders = int(shaders.size());
    report.images = imageNodes;
    report.triangles = 0;
    for (const auto &entry : meshes)
        report.triangles += entry.second.triangles;
    return changed;
}

bool SceneTranslator::translateCamera(const CameraInput &camera)
{
    if (cameraStated && camera.width == lastCamera.width && camera.height == lastCamera.height
        && std::memcmp(camera.view, lastCamera.view, sizeof(camera.view)) == 0
        && std::memcmp(camera.proj, lastCamera.proj, sizeof(camera.proj)) == 0)
        return false;
    cameraStated = true;
    lastCamera = camera;

    ccl::Camera *cam = scene->camera;
    const float *p = camera.proj;
    cam->set_full_width(camera.width);
    cam->set_full_height(camera.height);

    // Cycles' camera looks down its own +Z; a GL eye space looks down
    // -Z. Blender's sync flips the same axis on its camera matrix, and
    // the flip is what keeps the image unmirrored: eye x stays x.
    ccl::Transform camToWorld = ccl::transform_inverse(toTransform(camera.view))
                                * ccl::transform_scale(1.0f, 1.0f, -1.0f);
    cam->set_matrix(camToWorld);

    // The projection is read back into what Cycles wants stated: the
    // clip range, and the viewplane -- the screen-space rectangle the
    // raster covers. Off-centre projections (a boxZoom, a tiled
    // capture) survive that way; a field of view alone would not
    // carry them.
    const bool ortho = p[15] != 0.0f;
    // The background graph carries a camera-ray fan only for an
    // orthographic camera (translateWorld); crossing the projection
    // kinds invalidates it. The caller follows with refreshWorld()
    // when it restates the camera alone.
    if (worldStated && ortho != worldOrtho)
        worldStated = false;
    cameraOrtho = ortho;
    if (ortho) {
        cam->set_camera_type(ccl::CAMERA_ORTHOGRAPHIC);
        cam->set_nearclip((1.0f + p[14]) / p[10]);
        cam->set_farclip((p[14] - 1.0f) / p[10]);
        // x_ndc = p0 x + p12: the plane in camera units.
        cam->set_viewplane_left((-1.0f - p[12]) / p[0]);
        cam->set_viewplane_right((1.0f - p[12]) / p[0]);
        cam->set_viewplane_bottom((-1.0f - p[13]) / p[5]);
        cam->set_viewplane_top((1.0f - p[13]) / p[5]);
    }
    else {
        cam->set_camera_type(ccl::CAMERA_PERSPECTIVE);
        cam->set_nearclip(p[14] / (p[10] - 1.0f));
        cam->set_farclip(p[14] / (p[10] + 1.0f));
        // A 90 degree fov makes Cycles' perspective scale exactly one,
        // so the viewplane is stated in tangents: at z = -1 in eye
        // space, x_ndc = p0 x - p8.
        cam->set_fov(kPi * 0.5f);
        cam->set_viewplane_left((-1.0f + p[8]) / p[0]);
        cam->set_viewplane_right((1.0f + p[8]) / p[0]);
        cam->set_viewplane_bottom((-1.0f + p[9]) / p[5]);
        cam->set_viewplane_top((1.0f + p[9]) / p[5]);
    }
    cam->need_flags_update = true;
    cam->need_device_update = true;
    return true;
}

void SceneTranslator::refreshWorld()
{
    if (worldStated || !worldEverStated)
        return;
    translateWorld(worldPbr, worldOutput);
}

bool SceneTranslator::translateWorld(const PBRConfig &pbr, const OutputConfig &output)
{
    if (worldStated && pbr == worldPbr && output == worldOutput
        && worldOrtho == cameraOrtho)
        return false;
    worldStated = true;
    worldEverStated = true;
    worldOrtho = cameraOrtho;
    worldPbr = pbr;
    worldOutput = output;

    // The exposure is a multiplier on the linear frame before it is
    // encoded, and only a colour-managed frame has one
    // (OutputConfig::exposure); Cycles' film applies exactly that.
    scene->film->set_exposure(output.transform == OutputConfig::SRGB && output.exposure > 0.0f
                              ? output.exposure : 1.0f);

    // The environment lights the scene either way; whether it is SEEN
    // is envBackground -- which the engine honours only while PBR is
    // on (a Phong frame draws its gradient whatever the flag says), so
    // the same gate applies here. Where it is not seen, the film goes
    // transparent and the frame is composited over the host's own
    // background -- which is how the viewport will do it too (the host
    // draws its gradient, the Cycles image blits over it).
    scene->background->set_transparent(!(pbr.enabled && pbr.envBackground));

    // The environment as a LIGHT that is sampled, not only a backdrop
    // that is hit. Without a background light in the scene Cycles
    // never importance-samples the world (device_update_background:
    // "no background light found, signal renderer to skip sampling"),
    // so a sun in the picture is reached only when a BSDF sample
    // happens to point at it. That is unbiased in float and useless in
    // a frame: san_giuseppe_bridge.hdr holds 43 per cent of its
    // irradiance in texels above radiance 64 with a peak of 35,000, a
    // diffuse sample finds that disc about once in a hundred thousand,
    // and the hit lands in ONE pixel that clips at white. So the model
    // rendered without its sun -- darker and bluer than Blender's
    // Cycles, which creates this light for every world by default,
    // while the backdrop, a camera ray, agreed exactly
    // (docs/MaterialStorage.md 17.18). Made once; the importance map
    // follows the background shader through Shader::tag_update.
    if (!envLight) {
        auto *light = scene->create_node<ccl::BackgroundLight>();
        light->set_use_mis(true);
        light->set_map_resolution(0);
        light->set_is_enabled(true);
        ccl::array<ccl::Node *> used;
        used.push_back_slow(scene->default_background);
        light->set_used_shaders(used);
        auto *object = scene->create_node<ccl::Object>();
        object->set_tfm(ccl::transform_identity());
        object->set_visibility(ccl::PATH_RAY_VISIBILITY_ALL & ~ccl::PATH_RAY_VISIBILITY_CAMERA);
        object->set_geometry(light);
        envLight = light;
        envLightObject = object;
    }

    const int width = pbr.envImage && pbr.envImage->width > 0
        ? std::clamp(pbr.envImage->width, 256, 4096) : 1024;
    const int height = std::max(width / 2, 128);
    std::vector<float> pixels = bakeEnvironment(pbr, managed, width, height);
    ccl::ImageParams params;
    params.interpolation = ccl::INTERPOLATION_LINEAR;
    params.extension = ccl::EXTENSION_REPEAT;
    params.colorspace = ccl::u_colorspace_scene_linear;

    ccl::Shader *shader = scene->default_background;
    auto graph = std::make_unique<ccl::ShaderGraph>();
    // Every environment node in this graph, so the orthographic fan
    // below reaches all of them: with a blurred copy there are two,
    // and a camera-ray direction stated to one of them only would show
    // the two halves of the same sky in different places.
    std::vector<ccl::EnvironmentTextureNode*> envNodes;
    auto addEnv = [&](std::vector<float> px, int w, int h) {
        auto *node = graph->create_node<ccl::EnvironmentTextureNode>();
        node->handle = scene->image_manager->add_image(
            std::make_unique<BakedEnvironment>(std::move(px), w, h), params);
        node->set_projection(ccl::NODE_ENVIRONMENT_EQUIRECTANGULAR);
        node->set_colorspace(ccl::u_colorspace_scene_linear);
        envNodes.push_back(node);
        return node;
    };
    // One Is Camera Ray for the two things that ask it (the blur here
    // and the orthographic fan below), created only if something does.
    ccl::LightPathNode *path = nullptr;
    auto cameraRay = [&]() {
        if (!path)
            path = graph->create_node<ccl::LightPathNode>();
        return path->output("Is Camera Ray");
    };

    // The background blur (Render_PBREnvBlur), which the raster backend
    // does by reading a level of its cubemap. A path tracer cannot: the
    // world it samples IS the light, so softening it would relight the
    // scene, and an environment texture has no lod to read anyway. So
    // the softening is put where it belongs instead -- a second,
    // smaller bake of the same environment, mixed in on CAMERA rays
    // alone. Lighting, reflections and refractions keep the sharp
    // world; only what is seen behind the model changes. That is the
    // node graph Blender users build by hand for this (Blender ships no
    // control for it: its viewport Blur slider is the raster preview's
    // only), and it costs one small picture and three nodes.
    //
    // Is Camera Ray is 1 through a Transparent BSDF as well, so a
    // see-through pass-through shows the soft backdrop too. Same there.
    const int blurWidth = envBlurWidth(pbr.envBlur, width);
    ccl::ShaderOutput *envColor = nullptr;
    if (blurWidth > 0) {
        const int blurHeight = std::max(blurWidth / 2, 1);
        auto *soft = addEnv(downsampleEquirect(pixels, width, height,
                                               blurWidth, blurHeight),
                            blurWidth, blurHeight);
        auto *sharp = addEnv(std::move(pixels), width, height);
        auto *mix = graph->create_node<ccl::MixColorNode>();
        // Factor 0 is A, so A is the world every ray but the camera's
        // sees. Only the FACTOR is clamped by default (use_clamp);
        // use_clamp_result stays off, which is what lets a sky stay
        // brighter than one.
        graph->connect(sharp->output("Color"), mix->input("A"));
        graph->connect(soft->output("Color"), mix->input("B"));
        graph->connect(cameraRay(), mix->input("Factor"));
        envColor = mix->output("Result");
    }
    else {
        envColor = addEnv(std::move(pixels), width, height)->output("Color");
    }
    auto *bg = graph->create_node<ccl::BackgroundNode>();
    bg->set_strength(std::max(pbr.envIntensity, 0.0f));
    graph->connect(envColor, bg->input("Color"));
    graph->connect(bg->output("Background"), graph->output()->input("Surface"));

    // An orthographic camera has no per-pixel ray fan: every camera
    // ray shares the view direction, so the environment behind the
    // frame degenerates to one texel -- a flat wash where the raster
    // backend shows a readable sky (fs_fc_env.sc fakes a 45-degree
    // virtual field of view around the view axis there, tan 22.5 =
    // 0.41421356). Mirror that convention, and only for CAMERA rays:
    // lighting and reflections keep the true directions, exactly as
    // the raster meshes shade with real view vectors. Cycles' camera
    // space looks down +z (the flip translateCamera bakes into the
    // camera matrix), so the fan's axis is +1 where the GL shader has
    // -1; Window is the screen coordinate, (0,0) bottom left.
    if (cameraOrtho) {
        auto *window = graph->create_node<ccl::TextureCoordinateNode>();
        auto *fan = graph->create_node<ccl::VectorMathNode>();
        fan->set_math_type(ccl::NODE_VECTOR_MATH_MULTIPLY_ADD);
        const float t = 0.41421356f;
        fan->set_vector2(ccl::make_float3(2.0f * t, 2.0f * t, 0.0f));
        fan->set_vector3(ccl::make_float3(-t, -t, 1.0f));
        graph->connect(window->output("Window"), fan->input("Vector1"));
        auto *toWorld = graph->create_node<ccl::VectorTransformNode>();
        toWorld->set_transform_type(ccl::NODE_VECTOR_TRANSFORM_TYPE_VECTOR);
        toWorld->set_convert_from(
            ccl::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_CAMERA);
        toWorld->set_convert_to(ccl::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_WORLD);
        graph->connect(fan->output("Vector"), toWorld->input("Vector"));
        // The equirectangular lookup divides by the direction's length
        // (projection.h), so the fan needs no normalize node.
        auto *geom = graph->create_node<ccl::GeometryNode>();
        auto *mix = graph->create_node<ccl::MixVectorNode>();
        // A = Position restates the env node's own unlinked default
        // (LINK_POSITION; the ray direction in a background shader).
        graph->connect(geom->output("Position"), mix->input("A"));
        graph->connect(toWorld->output("Vector"), mix->input("B"));
        graph->connect(cameraRay(), mix->input("Factor"));
        for (auto *node : envNodes)
            graph->connect(mix->output("Result"), node->input("Vector"));
    }

    shader->set_graph(std::move(graph));
    shader->tag_update(scene);
    return true;
}

bool SceneTranslator::translateLight(const LightConfig &light,
                                     const float sceneMin[3],
                                     const float sceneMax[3])
{
    // A spot's power is stated at the distance to the scene centre, so
    // its bounds are part of what it was made from.
    const bool sameBounds = std::equal(sceneMin, sceneMin + 3, lightMin)
        && std::equal(sceneMax, sceneMax + 3, lightMax);
    if (lightStated && light == lastLight && (!light.spot || sameBounds))
        return false;
    lightStated = true;
    lastLight = light;
    std::copy(sceneMin, sceneMin + 3, lightMin);
    std::copy(sceneMax, sceneMax + 3, lightMax);

    // A light is remade, not restated: its type may change with the
    // config. The object goes before the light it references.
    bool changed = false;
    if (lightObject) {
        scene->delete_node(lightObject);
        lightObject = nullptr;
        changed = true;
    }
    if (lightNode) {
        scene->delete_node(lightNode);
        lightNode = nullptr;
        changed = true;
    }
    if (!light.valid)
        return changed;
    float color[4];
    unpackAuthored(light.color, color, managed);
    const float intensity = std::max(light.intensity, 0.0f);

    ccl::Light *node = nullptr;
    ccl::Transform tfm;
    if (light.spot) {
        auto *spot = scene->create_node<ccl::SpotLight>();
        // A point light's strength is radiant power; the config's
        // intensity is the irradiance it delivers at the scene, as a
        // sun's is. Convert at the distance to the scene centre so a
        // spot lights the model as brightly as the sun would.
        float centre[3];
        float dist2 = 0.0f;
        for (int i = 0; i < 3; ++i) {
            centre[i] = sceneMin[i] <= sceneMax[i]
                ? 0.5f * (sceneMin[i] + sceneMax[i]) : light.position[i];
            const float d = centre[i] - light.position[i];
            dist2 += d * d;
        }
        const float power = intensity * 4.0f * kPi * std::max(dist2, 1.0f);
        spot->set_strength(ccl::make_float3(color[0], color[1], color[2]) * power);
        spot->set_angle(2.0f * std::clamp(light.cutOffAngle, 0.01f, kPi * 0.5f));
        spot->set_smooth(std::clamp(light.dropOffRate, 0.0f, 1.0f));
        spot->set_radius(0.0f);
        // Cycles emits a spot along MINUS its frame's z.
        const float back[3] = {-light.direction[0], -light.direction[1], -light.direction[2]};
        tfm = frameAlongZ(back, light.position);
        node = spot;
    }
    else {
        auto *sun = scene->create_node<ccl::SunLight>();
        sun->set_strength(ccl::make_float3(color[0], color[1], color[2]) * intensity);
        // Half a degree, the real sun: enough to soften the shadow
        // edge the way the engine's filtered shadow map does.
        sun->set_angle(0.5f * kPi / 180.0f);
        // Cycles reads the direction TOWARD a sun off minus its z, so
        // the frame's z is the config's light-to-scene direction.
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        tfm = frameAlongZ(light.direction, origin);
        node = sun;
    }
    // LightConfig::shadow is NOT read, deliberately. It is Render_Shadow,
    // which is the raster backend's shadow MAP -- "a convenience switch
    // to drop shadows without leaving the Shadow display style", by its
    // own documentation, and a cost/technique knob rather than a
    // statement about the scene. A path tracer has no shadow map: its
    // shadow is what happens when a shadow ray meets the model, so
    // switching it off does not simplify a picture, it makes the light
    // pass through solid matter -- a picture of nothing. The same
    // reasoning keeps GTAO, cavity, matcap and bloom out of
    // Cycles::SceneInput (docs/MaterialStorage.md sec 17.13).
    node->set_cast_shadow(true);
    node->set_use_mis(true);
    ccl::array<ccl::Node *> used;
    used.push_back_slow(scene->default_light);
    node->set_used_shaders(used);

    ccl::Object *object = scene->create_node<ccl::Object>();
    object->set_tfm(tfm);
    object->set_visibility(ccl::PATH_RAY_VISIBILITY_ALL & ~ccl::PATH_RAY_VISIBILITY_CAMERA);
    object->set_geometry(node);
    lightNode = node;
    lightObject = object;
    return true;
}

SceneTranslator::Surface SceneTranslator::resolveSurface(const Material &m,
                                                         const PBRConfig &pbr,
                                                         const uint8_t *vertexColor,
                                                         const uint8_t *materialStream) const
{
    Surface s;
    float diffuse[4];
    if (vertexColor)
        unpackBytes(vertexColor, 4, diffuse, managed);
    else
        unpackAuthored(m.diffuse, diffuse, managed);
    float specular[4];
    float emissive[4];
    float shininess = m.shininess;
    float streamMetal = -1.0f;
    float streamRough = -1.0f;
    if (materialStream) {
        // rgba8 emissive, rgb8 specular + quantized shininess, then
        // the palette indices this translation does not read yet.
        unpackBytes(materialStream, 4, emissive, managed);
        unpackBytes(materialStream + 4, 3, specular, managed);
        shininess = materialStream[7] / 255.0f;
        if (m.perfacepbr) {
            streamMetal = materialStream[3] / 255.0f;
            streamRough = materialStream[7] / 255.0f;
        }
    }
    else {
        unpackAuthored(m.specular, specular, managed);
        unpackAuthored(m.emissive, emissive, managed);
    }

    std::copy(diffuse, diffuse + 3, s.base);
    s.alpha = 1.0f;
    if (m.transparent || vertexColor)
        s.alpha = diffuse[3];

    // Metalness: the material's own, the stream's, the frame's; and
    // where none of them states one, the Phong specular colour read as
    // material data (PBRConfig::fromSpecular).
    float metal = m.metallic >= 0.0f ? m.metallic : streamMetal >= 0.0f ? streamMetal : pbr.metallic;
    if (pbr.fromSpecular && m.metallic < 0.0f && streamMetal < 0.0f && pbr.metallic <= 0.0f)
        baseFromSpecular(diffuse, specular, s.base, metal);
    s.metallic = std::clamp(metal, 0.0f, 1.0f);

    float rough = m.roughness >= 0.0f ? m.roughness : streamRough >= 0.0f ? streamRough : pbr.roughness;
    if (rough <= 0.0f)
        rough = roughnessFromShininess(shininess, pbr.shininessMapping);
    s.roughness = std::clamp(rough, 0.02f, 1.0f);

    std::copy(emissive, emissive + 3, s.emissive);
    if (emissive[0] > 0.0f || emissive[1] > 0.0f || emissive[2] > 0.0f)
        s.emissiveStrength = 1.0f;

    if (!m.lighting) {
        s.unlit = true;
        s.emissiveStrength = 1.0f;
    }
    if (m.lightsource) {
        // A bulb: unshaded at its colour and bright by its intensity,
        // and in a path tracer it actually lights its surroundings.
        s.unlit = true;
        s.emissiveStrength = m.lightintensity > 0.0f ? m.lightintensity : 1.0f;
    }
    if (m.glass) {
        s.glass = true;
        s.ior = m.glassior > 0.0f ? m.glassior : 1.5f;
        s.glassRoughness = std::clamp(m.glassroughness, 0.0f, 1.0f);
        s.glassDensity = std::max(m.glassdensity, 0.0f);
        s.alpha = 1.0f;
    }
    return s;
}

float SceneTranslator::autoGlassDensity(const DrawCall &draw)
{
    const float dx = draw.bboxMax[0] - draw.bboxMin[0];
    const float dy = draw.bboxMax[1] - draw.bboxMin[1];
    const float dz = draw.bboxMax[2] - draw.bboxMin[2];
    if (dx < 0.0f || dy < 0.0f || dz < 0.0f)
        return 0.0f;
    const float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    return diag > 0.0f ? 3.0f / diag : 0.0f;
}

std::string SceneTranslator::Clip::key() const
{
    if (num == 0)
        return std::string();
    std::ostringstream key;
    key.precision(6);
    key << (concave ? "|c" : "|x");
    for (int i = 0; i < num; ++i)
        key << ':' << planes[i][0] << ',' << planes[i][1] << ',' << planes[i][2] << ','
            << planes[i][3];
    return key.str();
}

void SceneTranslator::connectSurface(ccl::ShaderGraph *graph,
                                     ccl::ShaderOutput *closure,
                                     const Clip &clip)
{
    ccl::ShaderInput *surface = graph->output()->input("Surface");
    if (clip.num == 0) {
        graph->connect(closure, surface);
        return;
    }
    // Cycles has no clip-plane state, so the graph decides. Each plane
    // reads as dot(P, n) + w -- positive on the kept side -- off the
    // Geometry node's world position, and the readings fold with a
    // minimum (survive EVERY plane) or, in concave mode, a maximum
    // (survive ONE), which is the parity the GL renderer and the bgfx
    // backend keep. Where the fold is negative the surface is swapped
    // for a transparent closure, which takes the point out of camera,
    // shadow and every other ray at once -- the section really cuts,
    // it is not painted over.
    auto *geo = graph->create_node<ccl::GeometryNode>();
    ccl::ShaderOutput *fold = nullptr;
    for (int i = 0; i < clip.num; ++i) {
        auto *dot = graph->create_node<ccl::VectorMathNode>();
        dot->set_math_type(ccl::NODE_VECTOR_MATH_DOT_PRODUCT);
        dot->set_vector2(
            ccl::make_float3(clip.planes[i][0], clip.planes[i][1], clip.planes[i][2]));
        graph->connect(geo->output("Position"), dot->input("Vector1"));
        auto *add = graph->create_node<ccl::MathNode>();
        add->set_math_type(ccl::NODE_MATH_ADD);
        add->set_value2(clip.planes[i][3]);
        graph->connect(dot->output("Value"), add->input("Value1"));
        if (!fold) {
            fold = add->output("Value");
            continue;
        }
        auto *combine = graph->create_node<ccl::MathNode>();
        combine->set_math_type(clip.concave ? ccl::NODE_MATH_MAXIMUM : ccl::NODE_MATH_MINIMUM);
        graph->connect(fold, combine->input("Value1"));
        graph->connect(add->output("Value"), combine->input("Value2"));
        fold = combine->output("Value");
    }
    auto *test = graph->create_node<ccl::MathNode>();
    test->set_math_type(ccl::NODE_MATH_LESS_THAN);
    test->set_value2(0.0f);
    graph->connect(fold, test->input("Value1"));
    auto *clear = graph->create_node<ccl::TransparentBsdfNode>();
    auto *mix = graph->create_node<ccl::MixClosureNode>();
    graph->connect(test->output("Value"), mix->input("Fac"));
    graph->connect(closure, mix->input("Closure1"));
    graph->connect(clear->output("BSDF"), mix->input("Closure2"));
    graph->connect(mix->output("Closure"), surface);
}

void SceneTranslator::debugSurface(ccl::ShaderGraph *graph,
                                   ccl::ShaderOutput *normal,
                                   const Clip &clip)
{
    if (!normal)
        normal = graph->create_node<ccl::GeometryNode>()->output("Normal");
    // Into camera space. Cycles' camera looks down its own +Z where a
    // GL eye looks down -Z (translateCamera's flip), so z is negated
    // on the way into the 0..1 encoding the raster path writes.
    auto *toCamera = graph->create_node<ccl::VectorTransformNode>();
    toCamera->set_transform_type(ccl::NODE_VECTOR_TRANSFORM_TYPE_NORMAL);
    toCamera->set_convert_from(ccl::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_WORLD);
    toCamera->set_convert_to(ccl::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_CAMERA);
    graph->connect(normal, toCamera->input("Vector"));
    auto *encode = graph->create_node<ccl::VectorMathNode>();
    encode->set_math_type(ccl::NODE_VECTOR_MATH_MULTIPLY_ADD);
    encode->set_vector2(ccl::make_float3(0.5f, 0.5f, -0.5f));
    encode->set_vector3(ccl::make_float3(0.5f, 0.5f, 0.5f));
    graph->connect(toCamera->output("Vector"), encode->input("Vector1"));
    auto *emission = graph->create_node<ccl::EmissionNode>();
    emission->set_strength(1.0f);
    graph->connect(encode->output("Vector"), emission->input("Color"));
    connectSurface(graph, emission->output("Emission"), clip);
}

std::string SceneTranslator::Maps::key() const
{
    if (!any())
        return std::string();
    std::ostringstream k;
    k.precision(4);
    k << ":tex" << (base ? base->textureId : 0) << '/' << int(model) << '/' << alphaSource;
    if (base && model == TextureImage::Blend)
        k << '/' << blendColor[0] << ',' << blendColor[1] << ',' << blendColor[2];
    k << ":bump" << (bump ? bump->textureId : 0) << '/' << bumpScale << '/' << uvScale
      << ":em" << (emissive ? emissive->textureId : 0)
      << ":mr" << (metalrough ? metalrough->textureId : 0);
    return k.str();
}

SceneTranslator::Maps SceneTranslator::resolveMaps(const Material &m,
                                                   const MeshData &mesh,
                                                   const BumpConfig &bump,
                                                   int start,
                                                   int count) const
{
    Maps maps;
    // Nothing to sample with: the raster path draws such a mesh
    // untextured too (its samplers read the corner texel of a white
    // stand-in), so the maps do not exist here either.
    if (!mesh.texCoords)
        return maps;
    maps.base = m.texture;
    maps.bump = m.bumpmap;
    maps.emissive = m.emissivemap;
    maps.metalrough = m.metallicroughnessmap;
    if (maps.base) {
        maps.model = maps.base->model;
        maps.alphaSource = maps.base->numComponents == 2 || maps.base->numComponents == 4;
        float c[4];
        unpackAuthored(maps.base->blendColor, c, managed);
        std::copy(c, c + 3, maps.blendColor);
    }
    maps.bumpScale = bump.scale;
    if (maps.bump) {
        const bool normalMap = maps.bump->numComponents >= 3;
        // A tangent-space normal map wants the tangents Cycles derives
        // from the UVs and the vertex normals; a mesh without normals
        // cannot have them, and shades unbumped as the safe fallback.
        if (normalMap && !mesh.normals) {
            maps.bump.reset();
        }
        else if (!normalMap) {
            // The raster path tilts the normal by strength * dh/dUV,
            // Cycles by distance * dh/dP with P in object units. The
            // two agree when distance is strength times the object
            // millimetres one UV unit spans, taken as the root of the
            // ratio of the range's area in the two spaces.
            double areaP = 0.0;
            double areaUV = 0.0;
            const int32_t *idx = mesh.triangleIndices + start;
            for (int i = 0; i + 2 < count; i += 3) {
                // The range is validated where the mesh is built
                // (translateDraw); this runs first, so it guards its
                // own reads.
                if (idx[i] < 0 || idx[i] >= mesh.numVertices || idx[i + 1] < 0
                    || idx[i + 1] >= mesh.numVertices || idx[i + 2] < 0
                    || idx[i + 2] >= mesh.numVertices)
                    continue;
                const float *p0 = mesh.positions + size_t(idx[i]) * 3;
                const float *p1 = mesh.positions + size_t(idx[i + 1]) * 3;
                const float *p2 = mesh.positions + size_t(idx[i + 2]) * 3;
                // (s, t, r, q) per vertex; the matrix and the divide are
                // a uniform scale away at most, which the ratio absorbs.
                const float *t0 = mesh.texCoords + size_t(idx[i]) * 4;
                const float *t1 = mesh.texCoords + size_t(idx[i + 1]) * 4;
                const float *t2 = mesh.texCoords + size_t(idx[i + 2]) * 4;
                const double e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
                const double e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                const double cx = e1[1] * e2[2] - e1[2] * e2[1];
                const double cy = e1[2] * e2[0] - e1[0] * e2[2];
                const double cz = e1[0] * e2[1] - e1[1] * e2[0];
                areaP += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
                areaUV += 0.5 * std::fabs(double(t1[0] - t0[0]) * double(t2[1] - t0[1])
                                          - double(t2[0] - t0[0]) * double(t1[1] - t0[1]));
            }
            maps.uvScale = areaUV > 1.0e-12 ? float(std::sqrt(areaP / areaUV)) : 0.0f;
        }
    }
    return maps;
}

void SceneTranslator::applyMaps(ccl::ShaderGraph *graph,
                                const Maps &maps,
                                SurfaceLinks &links,
                                float metallic,
                                float roughness,
                                const float emission[3])
{
    if (!maps.any())
        return;
    // The mesh's own coordinates (ATTR_STD_UV, with the texture matrix
    // already folded in when the mesh was built).
    auto *coords = graph->create_node<ccl::TextureCoordinateNode>();
    ccl::ShaderOutput *uv = coords->output("UV");
    auto image = [&](const std::shared_ptr<const TextureImage> &img, bool picture) {
        auto *node = graph->create_node<ccl::ImageTextureNode>();
        ccl::ImageParams params;
        params.interpolation = ccl::INTERPOLATION_LINEAR;
        params.extension = img->wrapS == TextureImage::Clamp ? ccl::EXTENSION_EXTEND
                                                              : ccl::EXTENSION_REPEAT;
        params.alpha_type = ccl::IMAGE_ALPHA_CHANNEL_PACKED;
        // A byte picture is sRGB-encoded, decoded per sample by the
        // kernel when the pipeline is colour managed (the PixelImage
        // note on why it is scene_linear_srgb and not srgb) and taken
        // raw when it is not (then, as everywhere, the two errors
        // cancel); a float picture is linear radiance already; a data
        // map is never decoded.
        params.colorspace = !picture ? ccl::u_colorspace_data
            : (managed && img->sample != TextureImage::F32) ? ccl::u_colorspace_scene_linear_srgb
                                                            : ccl::u_colorspace_scene_linear;
        node->handle = scene->image_manager->add_image(
            std::make_unique<PixelImage>(img, params.colorspace), params);
        node->set_colorspace(params.colorspace);
        node->set_extension(params.extension);
        node->set_alpha_type(params.alpha_type);
        node->set_interpolation(params.interpolation);
        graph->connect(uv, node->input("Vector"));
        ++imageNodes;
        return node;
    };
    auto value = [&](float v) {
        auto *node = graph->create_node<ccl::ValueNode>();
        node->set_value(v);
        return node->output("Value");
    };
    auto multiply = [&](ccl::ShaderOutput *a, ccl::ShaderOutput *b) {
        auto *node = graph->create_node<ccl::MathNode>();
        node->set_math_type(ccl::NODE_MATH_MULTIPLY);
        graph->connect(a, node->input("Value1"));
        graph->connect(b, node->input("Value2"));
        return node->output("Value");
    };
    // a op b, with a either a link or a constant vector.
    auto vectorMath = [&](ccl::NodeVectorMathType type,
                          ccl::ShaderOutput *a,
                          ccl::ShaderOutput *b,
                          const float *constant) {
        auto *node = graph->create_node<ccl::VectorMathNode>();
        node->set_math_type(type);
        if (a)
            graph->connect(a, node->input("Vector1"));
        else
            node->set_vector1(ccl::make_float3(constant[0], constant[1], constant[2]));
        graph->connect(b, node->input("Vector2"));
        return node->output("Vector");
    };

    if (maps.base) {
        // GL's texture environment on the lit colour, model by model as
        // fc_mesh_fs.sh applies it; the alpha is the fragment's
        // coverage and follows the same rules.
        auto *tex = image(maps.base, true);
        ccl::ShaderOutput *texel = tex->output("Color");
        ccl::ShaderOutput *texelAlpha = tex->output("Alpha");
        switch (maps.model) {
        case TextureImage::Decal: {
            auto *mix = graph->create_node<ccl::MixColorNode>();
            mix->set_blend_type(ccl::NODE_MIX_BLEND);
            mix->set_use_clamp(false);
            graph->connect(texelAlpha, mix->input("Factor"));
            graph->connect(links.base, mix->input("A"));
            graph->connect(texel, mix->input("B"));
            links.base = mix->output("Result");
            break;
        }
        case TextureImage::Blend: {
            // base * (1 - texel) + blendColor * texel, per channel.
            const float one[3] = {1.0f, 1.0f, 1.0f};
            ccl::ShaderOutput *inverse =
                vectorMath(ccl::NODE_VECTOR_MATH_SUBTRACT, nullptr, texel, one);
            ccl::ShaderOutput *kept =
                vectorMath(ccl::NODE_VECTOR_MATH_MULTIPLY, links.base, inverse, nullptr);
            ccl::ShaderOutput *blended =
                vectorMath(ccl::NODE_VECTOR_MATH_MULTIPLY, nullptr, texel, maps.blendColor);
            links.base = vectorMath(ccl::NODE_VECTOR_MATH_ADD, kept, blended, nullptr);
            links.alpha = multiply(links.alpha, texelAlpha);
            break;
        }
        case TextureImage::Replace:
            links.base = texel;
            if (maps.alphaSource)
                links.alpha = texelAlpha;
            break;
        default:  // Modulate
            links.base = vectorMath(ccl::NODE_VECTOR_MATH_MULTIPLY, links.base, texel, nullptr);
            links.alpha = multiply(links.alpha, texelAlpha);
            break;
        }
    }
    if (maps.emissive) {
        // Added after the texture environment, so the base picture does
        // not modulate the glow (glTF semantics, as the raster path has
        // it). A constant emission folds in as the addend's other side.
        auto *tex = image(maps.emissive, true);
        links.emission = vectorMath(ccl::NODE_VECTOR_MATH_ADD, links.emission,
                                    tex->output("Color"), emission);
    }
    if (maps.metalrough) {
        // glTF: green multiplies the roughness, blue the metallic.
        auto *tex = image(maps.metalrough, false);
        auto *split = graph->create_node<ccl::SeparateColorNode>();
        graph->connect(tex->output("Color"), split->input("Color"));
        links.roughness = multiply(links.roughness ? links.roughness : value(roughness),
                                   split->output("Green"));
        links.metallic = multiply(links.metallic ? links.metallic : value(metallic),
                                  split->output("Blue"));
    }
    if (maps.bump) {
        auto *tex = image(maps.bump, false);
        if (maps.bump->numComponents >= 3) {
            // Tangent-space normal map, Coin's (OpenGL's) convention;
            // the tangents come from the UVs and the vertex normals,
            // which Cycles derives itself when the node asks for them.
            auto *normalMap = graph->create_node<ccl::NormalMapNode>();
            normalMap->set_space(ccl::NODE_NORMAL_MAP_TANGENT);
            normalMap->set_strength(maps.bumpScale);
            graph->connect(tex->output("Color"), normalMap->input("Color"));
            links.normal = normalMap->output("Normal");
        }
        else {
            // Grayscale height. Cycles differentiates the height input
            // itself; the distance is the raster path's strength scaled
            // from per-UV to per-millimetre (resolveMaps).
            auto *bumpNode = graph->create_node<ccl::BumpNode>();
            bumpNode->set_strength(1.0f);
            bumpNode->set_distance(maps.bumpScale * (maps.uvScale > 0.0f ? maps.uvScale : 1.0f));
            graph->connect(tex->output("Color"), bumpNode->input("Height"));
            links.normal = bumpNode->output("Normal");
        }
    }
}

std::string SceneTranslator::Finish::key() const
{
    if (!any())
        return std::string();
    std::ostringstream k;
    k.precision(6);
    k << ":fin" << int(pattern) << '/' << pitch << '/' << depth << '/' << angle << '/'
      << int(frame.kind);
    if (frame.kind != SurfaceFrame::Unframed) {
        for (int i = 0; i < 3; ++i)
            k << '/' << frame.origin[i] << ',' << frame.axis[i] << ',' << frame.xdir[i];
        k << '/' << frame.radius;
    }
    return k.str();
}

SceneTranslator::Finish SceneTranslator::resolveFinish(const Material &m) const
{
    return resolveFinish(m.finish, m.finishpitch, m.finishdepth, m.finishangle, m.frame);
}

SceneTranslator::Finish SceneTranslator::resolveFinish(uint8_t pattern,
                                                       float pitch,
                                                       float depth,
                                                       float angleDeg,
                                                       const SurfaceFrame &frame)
{
    Finish f;
    // The bgfx path's gate (BGFXViewSubmit's setFinish): a pattern this
    // build knows, with a positive pitch and depth. A pattern a later
    // build wrote shades as none here as it does there.
    if (pattern == 0 || pattern > 5 || pitch <= 0.0f || depth <= 0.0f)
        return f;
    f.pattern = pattern;
    f.pitch = pitch;
    f.depth = depth;
    f.angle = angleDeg * kPi / 180.0f;
    f.frame = frame;
    return f;
}

void SceneTranslator::canonicalFrame(const SurfaceFrame &frame,
                                     float axis[3],
                                     float xdir[3],
                                     float ydir[3])
{
    // fcFinishFramed: the frame's own axes, the way the shader
    // canonicalizes them.
    axis[0] = frame.axis[0];
    axis[1] = frame.axis[1];
    axis[2] = frame.axis[2];
    float alen = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (alen < 1.0e-12f) {
        axis[0] = axis[1] = 0.0f;
        axis[2] = 1.0f;
        alen = 1.0f;
    }
    for (int i = 0; i < 3; ++i)
        axis[i] /= alen;
    const float ax = frame.xdir[0] * axis[0] + frame.xdir[1] * axis[1] + frame.xdir[2] * axis[2];
    xdir[0] = frame.xdir[0] - axis[0] * ax;
    xdir[1] = frame.xdir[1] - axis[1] * ax;
    xdir[2] = frame.xdir[2] - axis[2] * ax;
    float xlen = std::sqrt(xdir[0] * xdir[0] + xdir[1] * xdir[1] + xdir[2] * xdir[2]);
    if (xlen < 1.0e-12f) {
        // A degenerate x: any perpendicular will do, as the shader's
        // normalize of a zero vector would not.
        const float helper[3] = {std::fabs(axis[0]) < 0.9f ? 1.0f : 0.0f,
                                 std::fabs(axis[0]) < 0.9f ? 0.0f : 1.0f, 0.0f};
        xdir[0] = helper[1] * axis[2] - helper[2] * axis[1];
        xdir[1] = helper[2] * axis[0] - helper[0] * axis[2];
        xdir[2] = helper[0] * axis[1] - helper[1] * axis[0];
        xlen = std::sqrt(xdir[0] * xdir[0] + xdir[1] * xdir[1] + xdir[2] * xdir[2]);
    }
    for (int i = 0; i < 3; ++i)
        xdir[i] /= xlen;
    ydir[0] = axis[1] * xdir[2] - axis[2] * xdir[1];
    ydir[1] = axis[2] * xdir[0] - axis[0] * xdir[2];
    ydir[2] = axis[0] * xdir[1] - axis[1] * xdir[0];
}

void SceneTranslator::applyFinish(ccl::ShaderGraph *graph, const Finish &fin, SurfaceLinks &links)
{
    if (!fin.any())
        return;
    using Out = ccl::ShaderOutput *;
    enum Pattern : uint8_t { Knurl = 1, KnurlStraight = 2, Brushed = 3, Blasted = 4, Turned = 5 };

    const GraphOps ops{graph};
    // A table sampled at a coordinate: float texels, cubic so the
    // slope the bump takes off it is continuous, repeating so the
    // table is a period.
    auto table = [&](const std::shared_ptr<const std::vector<float>> &data,
                     int width,
                     int height,
                     int channels,
                     const char *label,
                     Out coord) {
        auto *node = graph->create_node<ccl::ImageTextureNode>();
        ccl::ImageParams params;
        params.interpolation = ccl::INTERPOLATION_CUBIC;
        params.extension = ccl::EXTENSION_REPEAT;
        params.alpha_type = ccl::IMAGE_ALPHA_CHANNEL_PACKED;
        params.colorspace = ccl::u_colorspace_data;
        node->handle = scene->image_manager->add_image(
            std::make_unique<TableImage>(data, width, height, channels, label), params);
        node->set_colorspace(params.colorspace);
        node->set_extension(params.extension);
        node->set_alpha_type(params.alpha_type);
        node->set_interpolation(params.interpolation);
        graph->connect(coord, node->input("Vector"));
        auto *split = graph->create_node<ccl::SeparateColorNode>();
        graph->connect(node->output("Color"), split->input("Color"));
        return split;
    };
    const float pitch = fin.pitch;
    const float depth = fin.depth;
    // The groove height at x millimetres across the lay: depth times
    // the unit profile over x / pitch.
    auto groove = [&](Out x) {
        Out coord = ops.combine(ops.mul(x, 1.0f / pitch), nullptr, 0.5f);
        return table(finishProfile(), kProfileSamples, 1, 1, "FreeCAD finish profile", coord)
            ->output("Red");
    };
    // The 1-D noise of lane `channel` at x / scale lattice units.
    auto noise1 = [&](Out x, float scale, const char *channel) {
        Out coord = ops.combine(ops.mul(x, 1.0f / (scale * kNoise1Cells)), nullptr, 0.5f);
        return table(finishNoise1(), kNoise1Cells * kNoise1Sub, 1, 4, "FreeCAD finish noise", coord)
            ->output(channel);
    };
    // The 2-D noise at q / scale lattice units, offset by (ox, oy) cells.
    auto noise2 = [&](Out qx, Out qy, float scale, float ox, float oy) {
        const float inv = 1.0f / (scale * kNoise2Cells);
        Out u = ops.add(ops.mul(qx, inv), ox / kNoise2Cells);
        Out v = ops.add(ops.mul(qy, inv), oy / kNoise2Cells);
        const int side = kNoise2Cells * kNoise2Sub;
        return table(finishNoise2(), side, side, 1, "FreeCAD finish craters",
                     ops.combine(u, v, 0.0f))
            ->output("Red");
    };

    // fcFinishPattern as a height: the same patterns over the same lay
    // coordinate q (millimetres), integrated.
    auto height = [&](Out qx, Out qy, uint8_t pattern) -> Out {
        switch (pattern) {
        case Knurl: {
            // Two trains crossing at a right angle, each half as deep.
            const float k = 0.70710678f;
            Out a = ops.madd(qx, k, ops.mul(qy, k));
            Out b = ops.madd(qx, k, ops.mul(qy, -k));
            return ops.mul(ops.math(ccl::NODE_MATH_ADD, groove(a), groove(b)), 0.5f * depth);
        }
        case Brushed: {
            // Scratches along the lay at two widths, fading in and out
            // along it: depth * (0.6 + nl(y)) * (0.7 n1(x) + 0.3 n2(x)).
            Out n1 = noise1(qx, pitch, "Red");
            Out n2 = noise1(qx, pitch * 0.37f, "Green");
            Out nl = noise1(qy, pitch * 60.0f, "Blue");
            Out h = ops.madd(n1, 0.7f, ops.mul(n2, 0.3f));
            return ops.mul(ops.math(ccl::NODE_MATH_MULTIPLY, ops.add(nl, 0.6f), h), depth);
        }
        case Blasted: {
            // Isotropic craters, two octaves; the lay cancels.
            Out n1 = noise2(qx, qy, pitch, 0.0f, 0.0f);
            Out n2 = noise2(qx, qy, pitch * 0.41f, 17.0f, 5.0f);
            return ops.mul(ops.madd(n1, 0.75f, ops.mul(n2, 0.25f)), depth);
        }
        case Turned: {
            // Concentric about the lay frame's origin: the train in
            // the radius.
            Out r2 = ops.math(ccl::NODE_MATH_ADD, ops.math(ccl::NODE_MATH_MULTIPLY, qx, qx),
                              ops.math(ccl::NODE_MATH_MULTIPLY, qy, qy));
            return ops.mul(groove(ops.math(ccl::NODE_MATH_SQRT, r2, nullptr)), depth);
        }
        default:
            // Straight knurl: one train, grooves along the lay.
            return ops.mul(groove(qx), depth);
        }
    };
    // Into the lay frame: q = R' p for the lay angle whose cosine and
    // sine are ca, sa.
    auto lay = [&](Out px, Out py, float ca, float sa, Out &qx, Out &qy) {
        qx = ops.madd(px, ca, ops.mul(py, sa));
        qy = ops.madd(px, -sa, ops.mul(py, ca));
    };

    // The object-space position: what the pattern is a function of, so
    // that an instanced or scaled copy carries the same finish. The
    // Bump node's two extra evaluations offset it by the ray
    // differentials, which is what makes the height a slope.
    auto *coords = graph->create_node<ccl::TextureCoordinateNode>();
    Out P = coords->output("Object");
    const float ca = std::cos(fin.angle);
    const float sa = std::sin(fin.angle);
    const SurfaceFrame &frame = fin.frame;
    Out H = nullptr;

    if (frame.kind == SurfaceFrame::Planar || frame.kind == SurfaceFrame::Radial) {
        float axis[3];
        float xdir[3];
        float ydir[3];
        canonicalFrame(frame, axis, xdir, ydir);
        Out d = ops.vec(ccl::NODE_VECTOR_MATH_SUBTRACT, P, frame.origin)->output("Vector");
        Out px = ops.dot(d, xdir);
        Out py = ops.dot(d, ydir);
        Out qx = nullptr;
        Out qy = nullptr;
        if (frame.kind == SurfaceFrame::Planar) {
            lay(px, py, ca, sa, qx, qy);
            H = height(qx, qy, fin.pattern);
        }
        else {
            // Radial: (arc length about the axis, distance along it),
            // the arc snapped to a whole number of pattern periods
            // round the reference radius so the atan2 seam closes (the
            // argument is in fc_finish.sh).
            Out z = ops.dot(d, axis);
            Out theta = ops.math(ccl::NODE_MATH_ARCTAN2, py, px);
            const float period = fin.pattern == Knurl ? pitch * 1.41421356f : pitch;
            Out arc = nullptr;
            if (frame.radius > 1.0e-6f) {
                const float cycles =
                    std::max(1.0f, std::floor(kTwoPi * frame.radius / period + 0.5f));
                arc = ops.mul(theta, cycles * period / kTwoPi);
            }
            else {
                // No reference radius: the fragment's own, as the
                // shader does. The snap then steps with the radius,
                // and the bump's finite difference sees the step where
                // the shader's analytic gradient did not -- no producer
                // states a radial frame without a radius, so this is
                // the shader's arithmetic kept rather than a case met.
                Out r = ops.math(ccl::NODE_MATH_SQRT,
                             ops.math(ccl::NODE_MATH_ADD, ops.math(ccl::NODE_MATH_MULTIPLY, px, px),
                                  ops.math(ccl::NODE_MATH_MULTIPLY, py, py)),
                             nullptr);
                Out cycles = ops.math(ccl::NODE_MATH_MAXIMUM,
                                      ops.math(ccl::NODE_MATH_FLOOR,
                                               ops.add(ops.mul(r, kTwoPi / period), 0.5f),
                                               nullptr),
                                      nullptr, 0.0f, 1.0f);
                arc = ops.math(ccl::NODE_MATH_MULTIPLY, theta, ops.mul(cycles, period / kTwoPi));
            }
            // Turning is fixed to the axis whatever the lay says: feed
            // marks run ROUND the work, which is the straight knurl
            // with the lay a quarter turn over.
            uint8_t pattern = fin.pattern;
            float pca = ca;
            float psa = sa;
            if (pattern == Turned) {
                pattern = KnurlStraight;
                pca = -sa;
                psa = ca;
            }
            lay(arc, z, pca, psa, qx, qy);
            H = height(qx, qy, pattern);
        }
    }
    else {
        // Triplanar: three axis-aligned projections weighted off the
        // object-space shading normal, the weights as the shader
        // shapes them (max(|n| - 0.25, 0)^4, normalized). The Bump
        // node's offsets leave the normal alone, so the weights are
        // constant across its three samples as the shader's chain
        // rule takes them to be.
        auto *n = ops.vec(ccl::NODE_VECTOR_MATH_ABSOLUTE, coords->output("Normal"), nullptr);
        auto *w = ops.separate(n->output("Vector"));
        Out t[3];
        const char *axes[3] = {"X", "Y", "Z"};
        for (int i = 0; i < 3; ++i) {
            Out c = ops.math(ccl::NODE_MATH_MAXIMUM, ops.add(w->output(axes[i]), -0.25f),
                             nullptr, 0.0f, 0.0f);
            Out c2 = ops.math(ccl::NODE_MATH_MULTIPLY, c, c);
            t[i] = ops.math(ccl::NODE_MATH_MULTIPLY, c2, c2);
        }
        Out sum = ops.math(ccl::NODE_MATH_ADD, ops.math(ccl::NODE_MATH_ADD, t[0], t[1]), t[2]);
        auto *p = ops.separate(P);
        // Each plane's two axes in the shader's order: yz, zx, xy.
        const char *first[3] = {"Y", "Z", "X"};
        const char *second[3] = {"Z", "X", "Y"};
        for (int i = 0; i < 3; ++i) {
            Out qx = nullptr;
            Out qy = nullptr;
            lay(p->output(first[i]), p->output(second[i]), ca, sa, qx, qy);
            Out hi = height(qx, qy, fin.pattern);
            Out wi = ops.math(ccl::NODE_MATH_DIVIDE, t[i], sum);
            Out term = ops.math(ccl::NODE_MATH_MULTIPLY, wi, hi);
            H = H ? ops.math(ccl::NODE_MATH_ADD, H, term) : term;
        }
    }

    // The height in millimetres of object space, differenced against
    // the ray differentials: with the distance and strength at one the
    // node computes normalize(|det| N - sign(det) surfgrad), which is
    // fcFinishPerturb. No footprint fade and no roughness hand-off:
    // a path tracer supersamples what the raster shader had to filter.
    auto *bump = graph->create_node<ccl::BumpNode>();
    bump->set_strength(1.0f);
    bump->set_distance(1.0f);
    graph->connect(H, bump->input("Height"));
    if (links.normal)
        graph->connect(links.normal, bump->input("Normal"));
    links.normal = bump->output("Normal");
}

std::string SceneTranslator::FaceImage::key() const
{
    if (!any())
        return std::string();
    std::ostringstream k;
    k.precision(6);
    k << ":face" << image->textureId << '/' << scale;
    if (scale > 0.0f) {
        k << '/' << int(frame.kind);
        if (frame.kind != SurfaceFrame::Unframed) {
            for (int i = 0; i < 3; ++i)
                k << '/' << frame.origin[i] << ',' << frame.axis[i] << ',' << frame.xdir[i];
            k << '/' << frame.radius;
        }
    }
    else {
        k << '/' << (meshUV ? "uv" : "0");
    }
    return k.str();
}

SceneTranslator::FaceImage SceneTranslator::resolveFaceImage(const Material &m,
                                                             const MeshData &mesh,
                                                             int layer,
                                                             const SurfaceFrame &frame) const
{
    FaceImage face;
    if (layer <= 0 || !m.texturepalette || m.texturepalette->entries.empty())
        return face;
    // Layer i is entry i - 1; a layer past the palette reads its last
    // entry, which is the raster path's clamp (fc_mesh_fs.sh).
    const auto &entries = m.texturepalette->entries;
    const size_t num = std::min(entries.size(), size_t(MaxFaceTexturePalette));
    face.image = entries[std::min(size_t(layer), num) - 1];
    if (!face.image)
        return face;
    face.scale = m.facetexscale;
    face.frame = frame;
    face.meshUV = mesh.texCoords != nullptr;
    return face;
}

void SceneTranslator::applyFaceImage(ccl::ShaderGraph *graph,
                                     const FaceImage &face,
                                     SurfaceLinks &links)
{
    if (!face.any())
        return;
    using Out = ccl::ShaderOutput *;
    const GraphOps ops{graph};

    // Where the image is sampled: fcFrameTexUV when the draw states a
    // tile size -- coordinates, not a gradient, so the sampler filters
    // them -- else the mesh's own coordinates (ATTR_STD_UV, the
    // texture matrix folded in as for the maps), else the corner
    // texel, which is what a mesh without coordinates reads there.
    Out coord = nullptr;
    if (face.scale > 0.0f) {
        const float inv = 1.0f / std::max(face.scale, 1.0e-6f);
        auto *coords = graph->create_node<ccl::TextureCoordinateNode>();
        Out P = coords->output("Object");
        const SurfaceFrame &frame = face.frame;
        if (frame.kind == SurfaceFrame::Planar || frame.kind == SurfaceFrame::Radial) {
            float axis[3];
            float xdir[3];
            float ydir[3];
            canonicalFrame(frame, axis, xdir, ydir);
            Out d = ops.vec(ccl::NODE_VECTOR_MATH_SUBTRACT, P, frame.origin)->output("Vector");
            Out px = ops.dot(d, xdir);
            Out py = ops.dot(d, ydir);
            if (frame.kind == SurfaceFrame::Planar) {
                coord = ops.combine(ops.mul(px, inv), ops.mul(py, inv), 0.0f);
            }
            else {
                // Arc length about the axis and distance along it, the
                // arc snapped to a whole number of tiles round the
                // reference radius (the fragment's own without one) so
                // the atan2 seam does not cut the image mid-tile.
                Out z = ops.dot(d, axis);
                Out theta = ops.math(ccl::NODE_MATH_ARCTAN2, py, px);
                Out u = nullptr;
                if (frame.radius > 1.0e-6f) {
                    const float tiles =
                        std::max(1.0f, std::floor(kTwoPi * frame.radius * inv + 0.5f));
                    u = ops.mul(theta, tiles / kTwoPi);
                }
                else {
                    Out r = ops.math(ccl::NODE_MATH_SQRT,
                                     ops.math(ccl::NODE_MATH_ADD,
                                              ops.math(ccl::NODE_MATH_MULTIPLY, px, px),
                                              ops.math(ccl::NODE_MATH_MULTIPLY, py, py)),
                                     nullptr);
                    Out tiles = ops.math(
                        ccl::NODE_MATH_MAXIMUM,
                        ops.math(ccl::NODE_MATH_FLOOR,
                                 ops.add(ops.mul(r, kTwoPi * inv), 0.5f), nullptr),
                        nullptr, 0.0f, 1.0f);
                    u = ops.math(ccl::NODE_MATH_MULTIPLY, theta, ops.mul(tiles, 1.0f / kTwoPi));
                }
                coord = ops.combine(u, ops.mul(z, inv), 0.0f);
            }
        }
        else {
            // Unframed: the dominant object axis wins outright (a
            // coordinate cannot be blended the way the finish's height
            // is -- two projections averaged read as a ghost of the
            // image over itself), off the object-space shading normal.
            // Selectors of 0 or 1 built from strict comparisons:
            // sx = (wx >= wy) (wx >= wz), sy = (1 - sx) (wy >= wz),
            // sz the rest -- fcFrameTexUV's if-chain.
            auto *n = ops.vec(ccl::NODE_VECTOR_MATH_ABSOLUTE, coords->output("Normal"), nullptr);
            auto *w = ops.separate(n->output("Vector"));
            auto ge = [&](Out a, Out b) {
                return ops.math(ccl::NODE_MATH_SUBTRACT, nullptr,
                                ops.math(ccl::NODE_MATH_LESS_THAN, a, b), 1.0f, 0.0f);
            };
            Out sx = ops.math(ccl::NODE_MATH_MULTIPLY, ge(w->output("X"), w->output("Y")),
                              ge(w->output("X"), w->output("Z")));
            Out sy = ops.math(ccl::NODE_MATH_MULTIPLY,
                              ops.math(ccl::NODE_MATH_SUBTRACT, nullptr, sx, 1.0f, 0.0f),
                              ge(w->output("Y"), w->output("Z")));
            Out sz = ops.math(ccl::NODE_MATH_SUBTRACT,
                              ops.math(ccl::NODE_MATH_SUBTRACT, nullptr, sx, 1.0f, 0.0f), sy);
            auto *p = ops.separate(P);
            // (y, z) under sx, (z, x) under sy, (x, y) under sz.
            auto pick = [&](const char *a, const char *b, const char *c) {
                Out t = ops.math(ccl::NODE_MATH_MULTIPLY, sx, p->output(a));
                t = ops.math(ccl::NODE_MATH_ADD, t,
                             ops.math(ccl::NODE_MATH_MULTIPLY, sy, p->output(b)));
                t = ops.math(ccl::NODE_MATH_ADD, t,
                             ops.math(ccl::NODE_MATH_MULTIPLY, sz, p->output(c)));
                return ops.mul(t, inv);
            };
            coord = ops.combine(pick("Y", "Z", "X"), pick("Z", "X", "Y"), 0.0f);
        }
    }
    else if (face.meshUV) {
        auto *coords = graph->create_node<ccl::TextureCoordinateNode>();
        coord = coords->output("UV");
    }

    // A picture, like the base colour texture (applyMaps): decoded on
    // the way in when the pipeline is colour managed, its alpha
    // coverage and left alone.
    auto *node = graph->create_node<ccl::ImageTextureNode>();
    ccl::ImageParams params;
    params.interpolation = ccl::INTERPOLATION_LINEAR;
    params.extension = face.image->wrapS == TextureImage::Clamp ? ccl::EXTENSION_EXTEND
                                                                  : ccl::EXTENSION_REPEAT;
    params.alpha_type = ccl::IMAGE_ALPHA_CHANNEL_PACKED;
    params.colorspace = (managed && face.image->sample != TextureImage::F32)
        ? ccl::u_colorspace_scene_linear_srgb
        : ccl::u_colorspace_scene_linear;
    node->handle = scene->image_manager->add_image(
        std::make_unique<PixelImage>(face.image, params.colorspace), params);
    node->set_colorspace(params.colorspace);
    node->set_extension(params.extension);
    node->set_alpha_type(params.alpha_type);
    node->set_interpolation(params.interpolation);
    if (coord)
        graph->connect(coord, node->input("Vector"));
    else
        node->set_vector(ccl::make_float3(0.0f, 0.0f, 0.0f));
    ++imageNodes;

    // On top of whatever the unit-0 texture did, and modulating like
    // the default texture environment: a marking on a red part comes
    // out red-tinted, as it does through the ordinary texture path.
    links.base = ops.vec2(ccl::NODE_VECTOR_MATH_MULTIPLY, links.base, node->output("Color"));
    links.alpha = ops.math(ccl::NODE_MATH_MULTIPLY, links.alpha, node->output("Alpha"));
}

ccl::Shader *SceneTranslator::acquireShader()
{
    if (!spareShaders.empty()) {
        ccl::Shader *shader = spareShaders.back();
        spareShaders.pop_back();
        return shader;
    }
    return scene->create_node<ccl::Shader>();
}

bool SceneTranslator::releaseUnusedShaders()
{
    if (shaders.empty())
        return false;
    // Live is what a live mesh references; everything else in the map
    // is a key nothing shades under any more (the doc's example is a
    // dragged section plane, whose coefficients key every shader they
    // clip). The node cannot be deleted, so it is re-graphed empty --
    // dropping its image handles -- and parked for acquireShader.
    std::set<const ccl::Node *> live;
    for (const auto &entry : meshes) {
        const ccl::array<ccl::Node *> &used = entry.second.mesh->get_used_shaders();
        for (size_t i = 0; i < used.size(); ++i)
            live.insert(used[i]);
    }
    bool released = false;
    for (auto it = shaders.begin(); it != shaders.end();) {
        if (live.count(it->second.shader)) {
            ++it;
            continue;
        }
        ccl::Shader *shader = it->second.shader;
        shader->name = ccl::ustring("fc_spare");
        shader->set_graph(std::make_unique<ccl::ShaderGraph>());
        shader->tag_update(scene);
        imageNodes -= it->second.images;
        spareShaders.push_back(shader);
        it = shaders.erase(it);
        released = true;
    }
    return released;
}

ccl::Shader *SceneTranslator::uniformShader(const Surface &s,
                                            const Clip &clip,
                                            const Maps &maps,
                                            const Finish &finish,
                                            const FaceImage &face)
{
    // The base colour and the alpha are the object's, so the key is
    // everything else; two draws that differ only in colour share
    // the shader as they share the mesh.
    std::ostringstream key;
    key.precision(4);
    if (debugView)
        key << "dbg" << debugView << ':';
    key << (s.unlit ? "u" : s.glass ? "g" : "p") << ':' << s.metallic << ':' << s.roughness
        << ':' << s.emissive[0] << ',' << s.emissive[1] << ',' << s.emissive[2]
        << ':' << s.emissiveStrength << ':' << s.ior << ':' << s.glassRoughness << ':'
        << s.glassDensity << clip.key() << maps.key() << face.key() << finish.key();
    auto it = shaders.find(key.str());
    if (it != shaders.end())
        return it->second.shader;

    const int imagesBefore = imageNodes;
    ccl::Shader *shader = acquireShader();
    shader->name = ccl::ustring(key.str());
    auto graph = std::make_unique<ccl::ShaderGraph>();
    auto *info = graph->create_node<ccl::ObjectInfoNode>();
    const float roughness = s.glass ? s.glassRoughness : s.roughness;
    const float emission[3] = {s.emissive[0] * s.emissiveStrength,
                               s.emissive[1] * s.emissiveStrength,
                               s.emissive[2] * s.emissiveStrength};
    SurfaceLinks links;
    links.base = info->output("Color");
    links.alpha = info->output("Alpha");
    applyMaps(graph.get(), maps, links, s.metallic, roughness, emission);
    // The face's image is a colour, drawn on an unlit draw too; a
    // finish is a statement about how the surface shades, so an unlit
    // draw leaves it off, as the raster path does.
    applyFaceImage(graph.get(), face, links);
    if (!s.unlit)
        applyFinish(graph.get(), finish, links);
    if (debugView == 2) {
        debugSurface(graph.get(), links.normal, clip);
    }
    else if (s.unlit) {
        auto *emissionNode = graph->create_node<ccl::EmissionNode>();
        emissionNode->set_strength(s.emissiveStrength);
        graph->connect(links.base, emissionNode->input("Color"));
        connectSurface(graph.get(), emissionNode->output("Emission"), clip);
    }
    else {
        auto *bsdf = graph->create_node<ccl::PrincipledBsdfNode>();
        if (links.metallic)
            graph->connect(links.metallic, bsdf->input("Metallic"));
        else
            bsdf->set_metallic(s.metallic);
        if (links.roughness)
            graph->connect(links.roughness, bsdf->input("Roughness"));
        else
            bsdf->set_roughness(roughness);
        bsdf->set_ior(s.ior);
        if (s.glass)
            bsdf->set_transmission_weight(1.0f);
        if (links.emission) {
            // The map made the emission a colour to add at strength 1,
            // with the constant part (colour times strength) folded in.
            bsdf->set_emission_strength(1.0f);
            graph->connect(links.emission, bsdf->input("Emission Color"));
        }
        else if (s.emissiveStrength > 0.0f) {
            bsdf->set_emission_color(ccl::make_float3(s.emissive[0], s.emissive[1], s.emissive[2]));
            bsdf->set_emission_strength(s.emissiveStrength);
        }
        if (links.normal)
            graph->connect(links.normal, bsdf->input("Normal"));
        graph->connect(links.base, bsdf->input("Base Color"));
        if (!s.glass)
            graph->connect(links.alpha, bsdf->input("Alpha"));
        connectSurface(graph.get(), bsdf->output("BSDF"), clip);
        if (s.glass && s.glassDensity > 0.0f) {
            // The tint of a glass body is absorption over the path
            // through it, not a surface colour: the same Beer-Lambert
            // the bgfx glass pass applies over the front/back depth
            // interval, here as a homogeneous absorption volume the
            // path tracer integrates along the refracted path. Cycles'
            // absorption closure weighs (1 - Color) * Density, which is
            // fs_fc_glass.sc's sigma exactly, so the object colour
            // drives it unchanged. The volume is NOT put through the
            // clip test: that would read the position and make the
            // volume heterogeneous (ray marched), for a section of a
            // tinted body that then absorbs over its removed part too.
            auto *absorb = graph->create_node<ccl::AbsorptionVolumeNode>();
            absorb->set_density(s.glassDensity);
            graph->connect(info->output("Color"), absorb->input("Color"));
            graph->connect(absorb->output("Volume"), graph->output()->input("Volume"));
        }
    }
    shader->set_graph(std::move(graph));
    shader->tag_update(scene);
    shaders[key.str()] = ShaderEntry{shader, imageNodes - imagesBefore};
    return shader;
}

ccl::Shader *SceneTranslator::materialXShader(const UserShader &user, const Clip &clip)
{
#ifndef HAVE_MATERIALX
    (void)user;
    (void)clip;
    return nullptr;
#else
    // A document is its own identity: the file it came from when it
    // came from one, otherwise its text, AND which of its surfaces is
    // worn -- one document usually carries a whole asset's material set
    // (docs/MaterialStorage.md sec 17.13). Two draws sharing a material
    // share the shader, which is what keeps a document off the
    // per-draw path -- interpreting one costs a library import.
    const std::string identity =
        (user.sourcePath.empty() ? user.fragmentSource : user.sourcePath)
        + '\0' + user.surface;
    // A parameter is part of the shader here, not a uniform on it: the
    // path tracer has no uniforms, so a value becomes a ValueNode in
    // the graph (docs/CyclesIntegration.md sec 6.11) and two parameter
    // sets are two shaders. The document's own identity stays separate
    // from them, because a document that will not interpret will not
    // interpret at any value.
    std::string variant = identity;
    for (const auto &param : user.params) {
        variant += '|' + param.name;
        for (float v : param.values)
            variant += ' ' + std::to_string(v);
    }
    const std::string key = (debugView ? "dbg" + std::to_string(debugView) + ':'
                                       : std::string())
        + "mtlx:" + std::to_string(std::hash<std::string> {}(variant)) + clip.key();
    auto it = shaders.find(key);
    if (it != shaders.end())
        return it->second.shader;

    // A document that failed once fails the same way every restate,
    // and finding that out costs a data-library import each time.
    if (materialXFailed.count(identity))
        return nullptr;

    auto fail = [&](const std::string &why) -> ccl::Shader * {
        materialXFailed.insert(identity);
        if (materialXReported.insert(identity).second)
            Base::Console().Error("MaterialX material not rendered: %s\n", why.c_str());
        return nullptr;
    };

    std::string error;
    mx::DocumentPtr doc =
        Render::MaterialX::loadDocument(user.fragmentSource, user.sourcePath, error);
    if (!doc)
        return fail(error);
    // The document's declared inputs, overridden by the material's
    // Param_* values before anything reads it (sec 6.11).
    Render::MaterialX::applyInputs(doc, user.params, user.surface);

    // The graph is built BEFORE the scene node, because a Cycles
    // shader cannot be taken back: delete_node(Shader *) only clears
    // the reference count -- "don't delete unused shaders, not
    // supported", scene.cpp -- so a shader created for a document
    // that then fails to interpret stays in scene->shaders with a
    // null graph, and the next device update dereferences it
    // (ShaderManager::device_update_pre -> graph->output()). Creating
    // it only once there is something to put in it is the whole fix.
    auto graph = std::make_unique<ccl::ShaderGraph>();
    MaterialXResult built = buildMaterialXSurface(graph.get(), doc, user.surface);
    if (!built.surface)
        return fail(built.error);
    imageNodes += built.images;
    ccl::Shader *shader = acquireShader();
    shader->name = ccl::ustring(key);
    // Per DOCUMENT, not per surface: a note about the model a document
    // is authored against is true of every surface in it, and a set
    // like the chess set carries fifteen. The message is part of the
    // key, so two surfaces with different notes still both report.
    // Kept apart from materialXReported, which gates the FAILURE
    // report and has to stay per surface -- one surface of a document
    // can fail where another does not.
    {
        const std::string document =
            user.sourcePath.empty() ? user.fragmentSource : user.sourcePath;
        for (const auto &w : built.warnings) {
            if (materialXNoted.insert(document + '\0' + w).second)
                Base::Console().Warning("MaterialX: %s\n", w.c_str());
        }
    }
    connectSurface(graph.get(), built.surface, clip);
    if (built.volume)
        graph->connect(built.volume, graph->output()->input("Volume"));
    shader->set_graph(std::move(graph));
    shader->tag_update(scene);
    shaders[key] = ShaderEntry{shader, built.images};
    return shader;
#endif
}

ccl::Shader *SceneTranslator::attributeShader(const Clip &clip,
                                              const Maps &maps,
                                              const Finish &finish,
                                              const FaceImage &face)
{
    const std::string key = (debugView ? "dbg" + std::to_string(debugView) + ':' : std::string())
        + "fc_attributes" + clip.key() + maps.key() + face.key() + finish.key();
    auto it = shaders.find(key);
    if (it != shaders.end())
        return it->second.shader;
    // One graph for every per-vertex draw: the mesh carries the
    // resolved surface as attributes (fc_base, fc_pbr = metallic /
    // roughness / alpha, fc_emissive), which is what keeps a
    // thousand-colour vertex-painted mesh at one shader instead of
    // a thousand. The maps, when the draw has them, sample on top of
    // those attributes exactly as they do on top of the scalars.
    const int imagesBefore = imageNodes;
    ccl::Shader *shader = acquireShader();
    shader->name = ccl::ustring(key);
    auto graph = std::make_unique<ccl::ShaderGraph>();
    auto *base = graph->create_node<ccl::AttributeNode>();
    base->set_attribute(ccl::ustring("fc_base"));
    auto *pbrAttr = graph->create_node<ccl::AttributeNode>();
    pbrAttr->set_attribute(ccl::ustring("fc_pbr"));
    auto *emissive = graph->create_node<ccl::AttributeNode>();
    emissive->set_attribute(ccl::ustring("fc_emissive"));
    auto *split = graph->create_node<ccl::SeparateXYZNode>();
    graph->connect(pbrAttr->output("Vector"), split->input("Vector"));
    SurfaceLinks links;
    links.base = base->output("Color");
    links.metallic = split->output("X");
    links.roughness = split->output("Y");
    links.alpha = split->output("Z");
    links.emission = emissive->output("Color");
    const float none[3] = {0.0f, 0.0f, 0.0f};
    applyMaps(graph.get(), maps, links, 0.0f, 0.0f, none);
    applyFaceImage(graph.get(), face, links);
    applyFinish(graph.get(), finish, links);
    if (debugView == 2) {
        debugSurface(graph.get(), links.normal, clip);
    }
    else {
        auto *bsdf = graph->create_node<ccl::PrincipledBsdfNode>();
        bsdf->set_emission_strength(1.0f);
        graph->connect(links.base, bsdf->input("Base Color"));
        graph->connect(links.metallic, bsdf->input("Metallic"));
        graph->connect(links.roughness, bsdf->input("Roughness"));
        graph->connect(links.alpha, bsdf->input("Alpha"));
        graph->connect(links.emission, bsdf->input("Emission Color"));
        if (links.normal)
            graph->connect(links.normal, bsdf->input("Normal"));
        connectSurface(graph.get(), bsdf->output("BSDF"), clip);
    }
    shader->set_graph(std::move(graph));
    shader->tag_update(scene);
    shaders[key] = ShaderEntry{shader, imageNodes - imagesBefore};
    return shader;
}

bool SceneTranslator::translateDraw(const DrawCall &draw,
                                    const PBRConfig &pbr,
                                    const BumpConfig &bump,
                                    const SectionConfig &section,
                                    Spare &spare,
                                    RenderReport &report,
                                    bool &changed)
{
    const Material &m = draw.material;
    // A path tracer renders surfaces. Lines and points are the host's
    // overlay (docs/CyclesIntegration.md sec 5.1); a wireframe draw
    // style, an on-top draw and a navigation gizmo are not scene
    // content; a stand-in box is not the shape; and the effect bodies
    // are volumes the volumetric pass raymarches, not geometry.
    if (m.type != Material::Triangle || m.drawstyle != Material::DrawFilled)
        return false;
    if (m.ontop || draw.skipbounds || draw.standIn)
        return false;
    if (m.water || m.cloud || m.fire || m.fountain)
        return false;
    const MeshData *mesh = draw.mesh.get();
    if (!mesh || !mesh->positions || mesh->numTriangleIndices < 3)
        return false;

    int start = draw.indexStart;
    int count = draw.indexCount > 0 ? draw.indexCount : mesh->numTriangleIndices - start;
    if (start < 0 || count < 3 || start + count > mesh->numTriangleIndices)
        return false;
    count -= count % 3;

    const bool perVertex = m.pervertexcolor || (m.perfacematerial && mesh->materials);
    const uint8_t *stream = m.perfacematerial ? mesh->materials : nullptr;

    // The resolved surface of the draw's scalars; per-vertex draws
    // re-resolve per vertex below, with the scalars as the fallback
    // for what the streams do not carry.
    Surface uniform = resolveSurface(m, pbr, nullptr, nullptr);
    if (uniform.glass && uniform.glassDensity <= 0.0f)
        uniform.glassDensity = autoGlassDensity(draw);
    Clip clip;
    clip.num = std::min<uint8_t>(m.numclipplanes, Material::MaxClipPlanes);
    clip.concave = m.clipconcave;
    for (int i = 0; i < clip.num; ++i)
        std::copy(m.clipplanes[i], m.clipplanes[i] + 4, clip.planes[i]);
    const Maps maps = resolveMaps(m, *mesh, bump, start, count);
    const Finish finish = resolveFinish(m);
    const int32_t *idx = mesh->triangleIndices + start;

    // The per-face palettes (docs/CyclesIntegration.md sec 6.7). A
    // face names its finish, its projection frame and its image by
    // index out of the material stream's third slot; here every
    // combination the draw's triangles actually name becomes a shader
    // VARIANT of the draw's own graph with that entry's constants,
    // and the triangle carries the variant's slot -- Cycles' own
    // per-triangle shader index, the mechanism its material slots
    // ride, so the verified constant-folded graphs serve unchanged
    // and no select chain runs per sample. A draw with nothing to
    // index (no stream, or no palette to read into) has one variant.
    const bool hasFaceTex = m.texturepalette && !m.texturepalette->entries.empty();
    const bool faceFinish = stream && m.finishpalette;
    const bool faceFrame = stream && m.framepalette;
    const bool faceLayer = stream && hasFaceTex && m.facetexlayer < 0;
    // A "material"-stage MaterialX program replaces the whole surface:
    // the document states the material, so the draw's own colour,
    // maps, finish and face palettes are not consulted. A document
    // this build cannot interpret falls back to the stock material
    // rather than dropping the draw (the sandboxed-failure rule).
    const UserShader *mtlx = nullptr;
    if (m.usershader && m.usershader->dialect == UserShader::Dialect::MaterialX
        && m.usershader->stage == "material")
        mtlx = m.usershader.get();
    auto variantShader = [&](const Finish &fin, const FaceImage &face) -> ccl::Shader * {
        if (mtlx) {
            if (ccl::Shader *s = materialXShader(*mtlx, clip))
                return s;
        }
        return perVertex ? attributeShader(clip, maps, fin, face)
                         : uniformShader(uniform, clip, maps, fin, face);
    };
    std::vector<ccl::Shader *> variants;
    std::vector<int> triShader;  // per triangle; empty = every one slot 0
    if (!faceFinish && !faceFrame && !faceLayer) {
        // The draw's own layer when the producer resolved one (a
        // uniformly imaged shape, a single-face draw); an unresolved
        // layer with no stream to read is layer 0, the untextured face.
        const int layer = hasFaceTex ? std::max<int>(m.facetexlayer, 0) : 0;
        variants.push_back(variantShader(finish, resolveFaceImage(m, *mesh, layer, m.frame)));
    }
    else {
        std::unordered_map<uint32_t, int> slots;
        uint32_t lastPacked = 0xffffffffu;
        int lastSlot = 0;
        triShader.resize(size_t(count / 3));
        for (int t = 0; t < count / 3; ++t) {
            // The face's indices, read off the triangle's first corner
            // (all three carry the face's, as the raster path assumes
            // when it interpolates them).
            const int32_t v = idx[3 * t];
            if (v < 0 || v >= mesh->numVertices)
                return false;
            const uint8_t *ms = stream + size_t(v) * size_t(MeshData::MaterialStride);
            const uint32_t fi = faceFinish ? ms[8] : 0;
            const uint32_t fr = faceFrame ? ms[9] : 0;
            const uint32_t tl = faceLayer ? ms[10]
                : hasFaceTex          ? uint32_t(std::max<int>(m.facetexlayer, 0))
                                      : 0;
            const uint32_t packed = fi | (fr << 8) | (tl << 16);
            if (packed != lastPacked) {
                auto found = slots.find(packed);
                if (found != slots.end()) {
                    lastSlot = found->second;
                }
                else {
                    // The frame first: it lays out the finish and the
                    // image both. An index past a palette reads what
                    // the raster's zero-filled uniform array holds
                    // there -- no finish, no frame (triplanar).
                    SurfaceFrame frame = m.frame;
                    if (faceFrame) {
                        const auto &entries = m.framepalette->entries;
                        frame = fr < entries.size() ? entries[fr] : SurfaceFrame();
                    }
                    Finish fin = finish;
                    if (faceFinish) {
                        const auto &entries = m.finishpalette->entries;
                        fin = fi < entries.size()
                            ? resolveFinish(entries[fi].pattern, entries[fi].pitch,
                                            entries[fi].depth, entries[fi].angle, frame)
                            : Finish();
                    }
                    else {
                        fin.frame = frame;
                    }
                    lastSlot = int(variants.size());
                    variants.push_back(
                        variantShader(fin, resolveFaceImage(m, *mesh, int(tl), frame)));
                    slots.emplace(packed, lastSlot);
                }
                lastPacked = packed;
            }
            triShader[size_t(t)] = lastSlot;
        }
    }
    // The mesh's UVs are wanted by the maps and by a face image the
    // draw lays out over them.
    const bool wantUV = maps.any() || (hasFaceTex && m.facetexscale <= 0.0f && mesh->texCoords);

    // Mesh identity: the cache contract (cacheId + generation names
    // the arrays), the index range, and what the shading needs baked
    // into the mesh -- the shader variants (the per-triangle slots
    // follow from them and the stream, which the generation names),
    // and for per-vertex draws every scalar the attributes were
    // resolved from.
    std::ostringstream key;
    key << mesh->cacheId << ':' << mesh->generation << ':' << start << ':' << count;
    for (ccl::Shader *shader : variants)
        key << ':' << static_cast<const void *>(shader);
    if (perVertex) {
        key.precision(4);
        key << ':' << m.diffuse << ':' << m.specular << ':' << m.emissive << ':' << m.shininess
            << ':' << m.metallic << ':' << m.roughness << ':' << m.transparent << ':'
            << m.perfacepbr << ':' << m.lighting << ':' << m.glass << ':' << m.lightsource;
    }
    if (maps.any() && !m.texidentity) {
        // The texture matrix is folded into the UV attribute, so a
        // transformed texture is a different mesh.
        key.precision(6);
        for (float f : m.texmatrix)
            key << ':' << f;
    }
    const std::string meshKey = key.str();

    ccl::Mesh *cmesh = nullptr;
    auto found = meshes.find(meshKey);
    if (found != meshes.end())
        cmesh = found->second.mesh;
    else {
        // Compact the vertex set the range touches.
        std::vector<int> remap(size_t(mesh->numVertices), -1);
        std::vector<int> verts;
        std::vector<int> tris;
        tris.reserve(size_t(count));
        for (int i = 0; i < count; ++i) {
            const int32_t v = idx[i];
            if (v < 0 || v >= mesh->numVertices)
                return false;
            if (remap[size_t(v)] < 0) {
                remap[size_t(v)] = int(verts.size());
                verts.push_back(v);
            }
            tris.push_back(remap[size_t(v)]);
        }

        cmesh = scene->create_node<ccl::Mesh>();
        cmesh->name = ccl::ustring(meshKey);
        ccl::array<ccl::Node *> used;
        for (ccl::Shader *shader : variants)
            used.push_back_slow(shader);
        cmesh->set_used_shaders(used);
        cmesh->resize_mesh(int(verts.size()), int(tris.size() / 3));

        ccl::packed_float3 *P = cmesh->get_position_for_write();
        for (size_t i = 0; i < verts.size(); ++i) {
            const float *p = mesh->positions + size_t(verts[i]) * 3;
            P[i] = ccl::packed_float3(ccl::make_float3(p[0], p[1], p[2]));
        }
        std::copy(tris.begin(), tris.end(), cmesh->get_triangles().data());
        if (triShader.empty())
            std::ranges::fill(cmesh->get_shader(), 0);
        else
            std::copy(triShader.begin(), triShader.end(), cmesh->get_shader().data());

        // The cache's vertex normals carry the smooth shading (hard
        // edges are already split vertices); without them the faces
        // shade flat, as they do in the host.
        bool smooth = mesh->normals != nullptr;
        if (smooth) {
            ccl::Attribute *attr = cmesh->attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
            // Storage is Cycles' packed types, not float3 (which is 16
            // bytes on SSE): packed_normal here, packed_float3 below.
            ccl::packed_normal *N = attr->data_for_write<ccl::packed_normal>();
            for (size_t i = 0; i < verts.size(); ++i) {
                const float *n = mesh->normals + size_t(verts[i]) * 3;
                ccl::float3 v = ccl::make_float3(n[0], n[1], n[2]);
                const float len = ccl::len(v);
                N[i] = ccl::packed_normal(len > 1.0e-12f ? v / len : ccl::make_float3(0.0f, 0.0f, 1.0f));
            }
        }
        std::ranges::fill(cmesh->get_smooth(), smooth);

        if (wantUV) {
            // The mesh's texture coordinates, per corner (ATTR_STD_UV).
            // They are Coin's (s, t, r, q) per vertex; the GL texture
            // matrix is applied here rather than in the graph -- a
            // mapping node cannot express a shear, the matrix can, and
            // folding it keeps the graph to one node -- and the
            // homogeneous divide follows, as it does in the raster path.
            ccl::Attribute *aUV = cmesh->attributes.add(ccl::ATTR_STD_UV);
            ccl::float2 *UV = aUV->data_for_write<ccl::float2>();
            for (int i = 0; i < count; ++i) {
                const float *t = mesh->texCoords + size_t(idx[i]) * 4;
                float x = t[0];
                float y = t[1];
                float w = t[3];
                if (!m.texidentity) {
                    const float *M = m.texmatrix;
                    x = M[0] * t[0] + M[4] * t[1] + M[8] * t[2] + M[12] * t[3];
                    y = M[1] * t[0] + M[5] * t[1] + M[9] * t[2] + M[13] * t[3];
                    w = M[3] * t[0] + M[7] * t[1] + M[11] * t[2] + M[15] * t[3];
                }
                const float inv = std::fabs(w) > 1.0e-12f ? 1.0f / w : 1.0f;
                UV[i] = ccl::make_float2(x * inv, y * inv);
            }
        }

        if (perVertex) {
            ccl::Attribute *aBase = cmesh->attributes.add(ccl::ustring("fc_base"), ccl::TypeColor,
                                                          ccl::ATTR_ELEMENT_VERTEX);
            ccl::Attribute *aPbr = cmesh->attributes.add(ccl::ustring("fc_pbr"), ccl::TypeVector,
                                                         ccl::ATTR_ELEMENT_VERTEX);
            ccl::Attribute *aEmissive = cmesh->attributes.add(ccl::ustring("fc_emissive"),
                                                              ccl::TypeColor,
                                                              ccl::ATTR_ELEMENT_VERTEX);
            ccl::packed_float3 *B = aBase->data_for_write<ccl::packed_float3>();
            ccl::packed_float3 *R = aPbr->data_for_write<ccl::packed_float3>();
            ccl::packed_float3 *E = aEmissive->data_for_write<ccl::packed_float3>();
            for (size_t i = 0; i < verts.size(); ++i) {
                const size_t v = size_t(verts[i]);
                const uint8_t *color = m.pervertexcolor && mesh->colors ? mesh->colors + v * 4 : nullptr;
                const uint8_t *ms = stream ? stream + v * size_t(MeshData::MaterialStride) : nullptr;
                const Surface s = resolveSurface(m, pbr, color, ms);
                B[i] = ccl::packed_float3(ccl::make_float3(s.base[0], s.base[1], s.base[2]));
                R[i] = ccl::packed_float3(ccl::make_float3(s.metallic, s.roughness, s.alpha));
                E[i] = ccl::packed_float3(ccl::make_float3(s.emissive[0] * s.emissiveStrength,
                                                           s.emissive[1] * s.emissiveStrength,
                                                           s.emissive[2] * s.emissiveStrength));
            }
        }

        cmesh->tag_triangles_modified();
        cmesh->tag_shader_modified();
        cmesh->tag_smooth_modified();
        meshes[meshKey] = MeshEntry{cmesh, long(tris.size() / 3)};
        ++report.built;
        changed = true;
    }

    // The instance: the object of the previous restate under this key
    // if one is spare, restated through its sockets (which tag nothing
    // when the value is the same -- a bolt that did not move costs no
    // update), else a new one.
    Instance inst;
    inst.key = meshKey + '|' + std::to_string(draw.objectKey);
    inst.mesh = cmesh;
    ccl::Object *object = nullptr;
    auto spareIt = spare.find(inst.key);
    if (spareIt != spare.end() && !spareIt->second.empty()) {
        object = spareIt->second.back();
        spareIt->second.pop_back();
    }
    bool created = false;
    if (!object) {
        object = scene->create_node<ccl::Object>();
        object->set_geometry(cmesh);
        created = true;
        ++report.added;
        changed = true;
    }
    object->set_tfm(draw.identity ? ccl::transform_identity() : toTransform(draw.model));
    object->set_color(ccl::make_float3(uniform.base[0], uniform.base[1], uniform.base[2]));
    object->set_alpha(uniform.alpha);
    if (object->is_modified()) {
        object->tag_update(scene);
        if (!created)
            ++report.restated;
        changed = true;
    }
    inst.object = object;
    instances.push_back(std::move(inst));

    translateCaps(draw, section, uniform, start, count, spare, report, changed);
    return true;
}

void SceneTranslator::translateCaps(const DrawCall &draw,
                                    const SectionConfig &section,
                                    const Surface &uniform,
                                    int start,
                                    int count,
                                    Spare &spare,
                                    RenderReport &report,
                                    bool &changed)
{
    const Material &m = draw.material;
    // The eligibility the bgfx backend uses for its stencil caps, so
    // the two engines cap the same draws: a whole-object draw of a
    // solid, sectioned, and the style asking for a fill.
    if (m.numclipplanes == 0 || draw.partIndex >= 0)
        return;
    if (!section.fill && !m.clipconcave)
        return;
    const MeshData *mesh = draw.mesh.get();
    if (!mesh || !(m.solidshape || mesh->hasSolid))
        return;

    // The cut face is the material's own surface in the fill colour:
    // never emissive, never transmissive (a glass cap would show
    // nothing, which is the one thing a cap must not do) and never
    // metallic, because the fill colour is a drawing convention, not
    // a measured reflectance. The colour rides the object, as every
    // other colour here does.
    Surface cap;
    cap.roughness = uniform.glass ? 0.5f : uniform.roughness;
    cap.alpha = uniform.alpha;
    float fill[4];
    unpackAuthored(section.fillInvert ? invertCapColor(m.diffuse) : m.diffuse, fill, managed);
    std::copy(fill, fill + 3, cap.base);

    const int planes = std::min<int>(m.numclipplanes, Material::MaxClipPlanes);
    for (int i = 0; i < planes; ++i) {
        // The cap of one plane is clipped by the remaining planes --
        // its own is excluded, which is both what the GL renderer
        // does and what keeps the cap off its own knife edge. Concave
        // mode leaves it unclipped, the state GL renders it in.
        Clip other;
        if (!m.clipconcave) {
            for (int j = 0; j < planes; ++j) {
                if (j == i)
                    continue;
                std::copy(m.clipplanes[j], m.clipplanes[j] + 4, other.planes[other.num]);
                ++other.num;
            }
        }
        ccl::Shader *shader = uniformShader(cap, other, Maps(), Finish(), FaceImage());

        // The plane pulled back into the mesh's own space, so that
        // the cap is built once for a placement and rides the draw's
        // transform like the geometry it caps.
        float local[4];
        if (draw.identity)
            std::copy(m.clipplanes[i], m.clipplanes[i] + 4, local);
        else {
            const float *M = draw.model;
            const float *n = m.clipplanes[i];
            local[0] = M[0] * n[0] + M[1] * n[1] + M[2] * n[2];
            local[1] = M[4] * n[0] + M[5] * n[1] + M[6] * n[2];
            local[2] = M[8] * n[0] + M[9] * n[1] + M[10] * n[2];
            local[3] = n[3] + M[12] * n[0] + M[13] * n[1] + M[14] * n[2];
        }

        std::ostringstream key;
        key.precision(6);
        key << mesh->cacheId << ':' << mesh->generation << ':' << start << ':' << count << "#cap:"
            << local[0] << ',' << local[1] << ',' << local[2] << ',' << local[3] << ':'
            << static_cast<const void *>(shader);
        const std::string meshKey = key.str();

        ccl::Mesh *cmesh = nullptr;
        auto found = meshes.find(meshKey);
        if (found != meshes.end())
            cmesh = found->second.mesh;
        else {
            const std::vector<float> tris = buildCapTriangles(*mesh, start, count, local);
            if (tris.size() < 9)
                continue;
            const int numtris = int(tris.size() / 9);
            cmesh = scene->create_node<ccl::Mesh>();
            cmesh->name = ccl::ustring(meshKey);
            ccl::array<ccl::Node *> used;
            used.push_back_slow(shader);
            cmesh->set_used_shaders(used);
            cmesh->resize_mesh(numtris * 3, numtris);
            ccl::packed_float3 *P = cmesh->get_position_for_write();
            for (int vi = 0; vi < numtris * 3; ++vi)
                P[vi] = ccl::packed_float3(
                    ccl::make_float3(tris[size_t(vi) * 3], tris[size_t(vi) * 3 + 1],
                                     tris[size_t(vi) * 3 + 2]));
            ccl::array<int> &indices = cmesh->get_triangles();
            for (int vi = 0; vi < numtris * 3; ++vi)
                indices[vi] = vi;
            std::ranges::fill(cmesh->get_shader(), 0);
            // Flat: the cut face IS flat, and its normal is the
            // triangles' own.
            std::ranges::fill(cmesh->get_smooth(), false);
            cmesh->tag_triangles_modified();
            cmesh->tag_shader_modified();
            cmesh->tag_smooth_modified();
            meshes[meshKey] = MeshEntry{cmesh, long(numtris)};
            ++report.built;
            changed = true;
        }

        Instance inst;
        inst.key = meshKey + '|' + std::to_string(draw.objectKey);
        inst.mesh = cmesh;
        ccl::Object *object = nullptr;
        auto spareIt = spare.find(inst.key);
        if (spareIt != spare.end() && !spareIt->second.empty()) {
            object = spareIt->second.back();
            spareIt->second.pop_back();
        }
        bool created = false;
        if (!object) {
            object = scene->create_node<ccl::Object>();
            object->set_geometry(cmesh);
            created = true;
            ++report.added;
            changed = true;
        }
        object->set_tfm(draw.identity ? ccl::transform_identity() : toTransform(draw.model));
        object->set_color(ccl::make_float3(cap.base[0], cap.base[1], cap.base[2]));
        object->set_alpha(cap.alpha);
        if (object->is_modified()) {
            object->tag_update(scene);
            if (!created)
                ++report.restated;
            changed = true;
        }
        inst.object = object;
        instances.push_back(std::move(inst));
    }
}

}  // namespace Render::Cycles
