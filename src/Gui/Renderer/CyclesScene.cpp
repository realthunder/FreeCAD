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
#include <memory>
#include <set>
#include <sstream>
#include <vector>

#include "CyclesSceneP.h"
#include "Environment.h"

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

/// The environment baked to an equirectangular float picture the
/// engine's image manager serves to the world shader. Baked, rather
/// than described, because the procedural presets are C++ functions
/// of direction (Environment.h) with no Cycles node equivalent, and
/// baking a user picture through the same sampler keeps both the
/// sphere-map convention and the sRGB decode in one place.
///
/// Layout follows the kernel's direction_to_equirectangular: column u
/// = 0.5 - atan2(y, x) / 2pi, row v = 1 - acos(z) / pi, row 0 at the
/// nadir.
class BakedEnvironment : public ccl::ImageLoader
{
public:
    BakedEnvironment(const PBRConfig &pbr, bool managed, int width, int height)
        : width(width)
        , height(height)
    {
        rgba.resize(size_t(width) * size_t(height) * 4);
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
    }

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

}  // namespace

SceneTranslator::SceneTranslator(ccl::Scene *scene, bool colorManaged)
    : scene(scene)
    , managed(colorManaged)
{}

bool SceneTranslator::translate(const SceneInput &input, RenderReport &report)
{
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
        if (!translateDraw(draw, input.pbr, input.section, spare, report, changed)) {
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

    changed |= translateLight(input.light, sceneMin, sceneMax);

    report.meshes = int(meshes.size());
    report.objects = int(instances.size());
    report.shaders = int(shaders.size());
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

bool SceneTranslator::translateWorld(const PBRConfig &pbr, const OutputConfig &output)
{
    if (worldStated && pbr == worldPbr && output == worldOutput)
        return false;
    worldStated = true;
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

    const int width = pbr.envImage && pbr.envImage->width > 0
        ? std::clamp(pbr.envImage->width, 256, 4096) : 1024;
    const int height = std::max(width / 2, 128);
    auto loader = std::make_unique<BakedEnvironment>(pbr, managed, width, height);
    ccl::ImageParams params;
    params.interpolation = ccl::INTERPOLATION_LINEAR;
    params.extension = ccl::EXTENSION_REPEAT;
    params.colorspace = ccl::u_colorspace_scene_linear;

    ccl::Shader *shader = scene->default_background;
    auto graph = std::make_unique<ccl::ShaderGraph>();
    auto *env = graph->create_node<ccl::EnvironmentTextureNode>();
    env->handle = scene->image_manager->add_image(std::move(loader), params);
    env->set_projection(ccl::NODE_ENVIRONMENT_EQUIRECTANGULAR);
    env->set_colorspace(ccl::u_colorspace_scene_linear);
    auto *bg = graph->create_node<ccl::BackgroundNode>();
    bg->set_strength(std::max(pbr.envIntensity, 0.0f));
    graph->connect(env->output("Color"), bg->input("Color"));
    graph->connect(bg->output("Background"), graph->output()->input("Surface"));
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
    node->set_cast_shadow(light.shadow);
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

ccl::Shader *SceneTranslator::uniformShader(const Surface &s, const Clip &clip)
{
    // The base colour and the alpha are the object's, so the key is
    // everything else; two draws that differ only in colour share
    // the shader as they share the mesh.
    std::ostringstream key;
    key.precision(4);
    key << (s.unlit ? "u" : s.glass ? "g" : "p") << ':' << s.metallic << ':' << s.roughness
        << ':' << s.emissive[0] << ',' << s.emissive[1] << ',' << s.emissive[2]
        << ':' << s.emissiveStrength << ':' << s.ior << ':' << s.glassRoughness << ':'
        << s.glassDensity << clip.key();
    auto it = shaders.find(key.str());
    if (it != shaders.end())
        return it->second;

    ccl::Shader *shader = scene->create_node<ccl::Shader>();
    shader->name = ccl::ustring(key.str());
    auto graph = std::make_unique<ccl::ShaderGraph>();
    auto *info = graph->create_node<ccl::ObjectInfoNode>();
    if (s.unlit) {
        auto *emission = graph->create_node<ccl::EmissionNode>();
        emission->set_strength(s.emissiveStrength);
        graph->connect(info->output("Color"), emission->input("Color"));
        connectSurface(graph.get(), emission->output("Emission"), clip);
    }
    else {
        auto *bsdf = graph->create_node<ccl::PrincipledBsdfNode>();
        bsdf->set_metallic(s.metallic);
        bsdf->set_roughness(s.glass ? s.glassRoughness : s.roughness);
        bsdf->set_ior(s.ior);
        if (s.glass)
            bsdf->set_transmission_weight(1.0f);
        if (s.emissiveStrength > 0.0f) {
            bsdf->set_emission_color(ccl::make_float3(s.emissive[0], s.emissive[1], s.emissive[2]));
            bsdf->set_emission_strength(s.emissiveStrength);
        }
        graph->connect(info->output("Color"), bsdf->input("Base Color"));
        if (!s.glass)
            graph->connect(info->output("Alpha"), bsdf->input("Alpha"));
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
    shaders[key.str()] = shader;
    return shader;
}

ccl::Shader *SceneTranslator::attributeShader(const Clip &clip)
{
    const std::string key = "fc_attributes" + clip.key();
    auto it = shaders.find(key);
    if (it != shaders.end())
        return it->second;
    // One graph for every per-vertex draw: the mesh carries the
    // resolved surface as attributes (fc_base, fc_pbr = metallic /
    // roughness / alpha, fc_emissive), which is what keeps a
    // thousand-colour vertex-painted mesh at one shader instead of
    // a thousand.
    ccl::Shader *shader = scene->create_node<ccl::Shader>();
    shader->name = ccl::ustring(key);
    auto graph = std::make_unique<ccl::ShaderGraph>();
    auto *base = graph->create_node<ccl::AttributeNode>();
    base->set_attribute(ccl::ustring("fc_base"));
    auto *pbrAttr = graph->create_node<ccl::AttributeNode>();
    pbrAttr->set_attribute(ccl::ustring("fc_pbr"));
    auto *emissive = graph->create_node<ccl::AttributeNode>();
    emissive->set_attribute(ccl::ustring("fc_emissive"));
    auto *split = graph->create_node<ccl::SeparateXYZNode>();
    auto *bsdf = graph->create_node<ccl::PrincipledBsdfNode>();
    bsdf->set_emission_strength(1.0f);
    graph->connect(pbrAttr->output("Vector"), split->input("Vector"));
    graph->connect(base->output("Color"), bsdf->input("Base Color"));
    graph->connect(split->output("X"), bsdf->input("Metallic"));
    graph->connect(split->output("Y"), bsdf->input("Roughness"));
    graph->connect(split->output("Z"), bsdf->input("Alpha"));
    graph->connect(emissive->output("Color"), bsdf->input("Emission Color"));
    connectSurface(graph.get(), bsdf->output("BSDF"), clip);
    shader->set_graph(std::move(graph));
    shader->tag_update(scene);
    shaders[key] = shader;
    return shader;
}

bool SceneTranslator::translateDraw(const DrawCall &draw,
                                    const PBRConfig &pbr,
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
    ccl::Shader *shader = perVertex ? attributeShader(clip) : uniformShader(uniform, clip);

    // Mesh identity: the cache contract (cacheId + generation names
    // the arrays), the index range, and what the shading needs baked
    // into the mesh -- the shader, and for per-vertex draws every
    // scalar the attributes were resolved from.
    std::ostringstream key;
    key << mesh->cacheId << ':' << mesh->generation << ':' << start << ':' << count << ':'
        << static_cast<const void *>(shader);
    if (perVertex) {
        key.precision(4);
        key << ':' << m.diffuse << ':' << m.specular << ':' << m.emissive << ':' << m.shininess
            << ':' << m.metallic << ':' << m.roughness << ':' << m.transparent << ':'
            << m.perfacepbr << ':' << m.lighting << ':' << m.glass << ':' << m.lightsource;
    }
    const std::string meshKey = key.str();

    ccl::Mesh *cmesh = nullptr;
    auto found = meshes.find(meshKey);
    if (found != meshes.end())
        cmesh = found->second.mesh;
    else {
        // Compact the vertex set the range touches.
        const int32_t *idx = mesh->triangleIndices + start;
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
        used.push_back_slow(shader);
        cmesh->set_used_shaders(used);
        cmesh->resize_mesh(int(verts.size()), int(tris.size() / 3));

        ccl::packed_float3 *P = cmesh->get_position_for_write();
        for (size_t i = 0; i < verts.size(); ++i) {
            const float *p = mesh->positions + size_t(verts[i]) * 3;
            P[i] = ccl::packed_float3(ccl::make_float3(p[0], p[1], p[2]));
        }
        std::copy(tris.begin(), tris.end(), cmesh->get_triangles().data());
        std::ranges::fill(cmesh->get_shader(), 0);

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
        ccl::Shader *shader = uniformShader(cap, other);

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
