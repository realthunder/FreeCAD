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

#include "CyclesRenderer.h"

#ifdef HAVE_CYCLES

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include <QImage>
#include <QString>

// Cycles headers: included on the cycles_embed target's terms (its
// include directories, CCL_NAMESPACE_BEGIN and the feature defines).
#include "device/device.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/pass.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "session/buffers.h"
#include "session/output_driver.h"
#include "session/session.h"
#include "util/array.h"
#include "util/path.h"
#include "util/transform.h"
#include "util/types.h"

#endif  // HAVE_CYCLES

namespace Render::Cycles {

#ifndef HAVE_CYCLES

bool available()
{
    return false;
}

std::vector<DeviceInfo> devices()
{
    return {};
}

bool renderTestScene(const std::string &, int, int, int, const std::string &, std::string *error)
{
    if (error)
        *error = "this build carries no Cycles engine (BUILD_CYCLES is off)";
    return false;
}

#else  // HAVE_CYCLES

namespace {

/// Process-wide engine setup, once. The engine's data root is the
/// renderer resource tree's cycles/ directory: the GPU devices compile
/// their kernels at runtime from source/kernel under it (the build
/// copies and installs that tree, docs/CyclesIntegration.md sec 4.1),
/// with nvcc found through CUDA_BIN_PATH as Cycles itself does. The
/// CPU device needs nothing from it. The kernel cache goes to the
/// engine's default user cache directory.
void initOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        ccl::path_init(Render::RendererFactory::resourcePath() + "cycles");
    });
}

/// Keeps the finished frame. Cycles calls write_render_tile() from its
/// own session thread once the whole buffer is sampled; the caller
/// reads the copy after Session::wait() returns, so no lock is needed
/// here -- wait() is the barrier.
class CaptureDriver : public ccl::OutputDriver
{
public:
    int width = 0;
    int height = 0;
    std::vector<float> pixels;  ///< RGBA float, linear, bottom-up
    bool done = false;

    void write_render_tile(const Tile &tile) override
    {
        // Only the full buffer; with tiling off there is exactly one.
        if (!(tile.size == tile.full_size))
            return;
        width = tile.size.x;
        height = tile.size.y;
        pixels.assign(size_t(width) * size_t(height) * 4, 0.0f);
        done = tile.get_pass_pixels("combined", 4, pixels.data());
    }
};

/// Linear to sRGB-encoded 8-bit, for the file. The viewport blit must
/// NOT do this: the engine's output transform encodes once for the
/// screen, and a pre-encoded frame is the double-encode trap of
/// docs/CyclesIntegration.md sec 6.1.
unsigned char encode(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    float e = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<unsigned char>(std::lround(e * 255.0f));
}

ccl::Shader *makeDiffuse(ccl::Scene *scene, const char *name, ccl::float3 color)
{
    ccl::Shader *shader = scene->create_node<ccl::Shader>();
    shader->name = ccl::ustring(name);
    auto graph = std::make_unique<ccl::ShaderGraph>();
    auto *bsdf = graph->create_node<ccl::DiffuseBsdfNode>();
    bsdf->set_color(color);
    graph->connect(bsdf->output("BSDF"), graph->output()->input("Surface"));
    shader->set_graph(std::move(graph));
    shader->tag_update(scene);
    return shader;
}

void addMesh(ccl::Scene *scene,
             ccl::Shader *shader,
             const std::vector<ccl::float3> &verts,
             const std::vector<int> &tris,
             const ccl::Transform &tfm)
{
    ccl::Mesh *mesh = scene->create_node<ccl::Mesh>();
    ccl::array<ccl::Node *> used;
    used.push_back_slow(shader);
    mesh->set_used_shaders(used);

    mesh->resize_mesh(int(verts.size()), int(tris.size() / 3));
    ccl::packed_float3 *P = mesh->get_position_for_write();
    for (size_t i = 0; i < verts.size(); ++i)
        P[i] = ccl::packed_float3(verts[i]);
    std::copy(tris.begin(), tris.end(), mesh->get_triangles().data());
    std::ranges::fill(mesh->get_smooth(), false);
    std::ranges::fill(mesh->get_shader(), 0);
    mesh->tag_triangles_modified();
    mesh->tag_shader_modified();
    mesh->tag_smooth_modified();

    ccl::Object *object = scene->create_node<ccl::Object>();
    object->set_geometry(mesh);
    object->set_tfm(tfm);
}

}  // namespace

bool available()
{
    return true;
}

std::vector<DeviceInfo> devices()
{
    initOnce();
    std::vector<DeviceInfo> out;
    for (const ccl::DeviceInfo &d : ccl::Device::available_devices())
        out.push_back({ccl::Device::string_from_type(d.type), d.description});
    return out;
}

bool renderTestScene(const std::string &path,
                     int width,
                     int height,
                     int samples,
                     const std::string &deviceType,
                     std::string *error)
{
    auto fail = [error](const std::string &message) {
        if (error)
            *error = message;
        return false;
    };
    if (width < 1 || height < 1 || samples < 1)
        return fail("width, height and samples must be positive");

    initOnce();

    ccl::DeviceType type = ccl::Device::type_from_string(deviceType.c_str());
    if (type == ccl::DEVICE_NONE)
        return fail("unknown device type '" + deviceType + "'");
    // DEVICE_MASK() spells its cast unqualified, for use inside ccl.
    ccl::vector<ccl::DeviceInfo> found =
        ccl::Device::available_devices(ccl::DeviceTypeMask(1 << type));
    if (found.empty())
        return fail("no " + deviceType + " device is available");

    ccl::SessionParams sp;
    sp.device = found.front();
    // Denoise where we render. Left unset, a default-constructed
    // DeviceInfo carries the CPU id with no description, and the
    // session's equality check on the two asserts in a debug build.
    sp.denoise_device = sp.device;
    sp.background = true;
    sp.headless = true;
    sp.samples = samples;
    sp.use_auto_tile = false;
    sp.tile_size = 0;
    ccl::SceneParams scp;

    // The session owns the scene and the driver; it is torn down at
    // the end of this scope, render done or not.
    auto session = std::make_unique<ccl::Session>(sp, scp);
    auto driver = std::make_unique<CaptureDriver>();
    CaptureDriver *capture = driver.get();
    session->set_output_driver(std::move(driver));

    ccl::Scene *scene = session->scene.get();

    // Camera: Cycles' camera looks down its local +Z with +Y up (the
    // bundled examples put it at z = -4 to see the origin). Sit it
    // back, a little up, tilted down at the cube.
    ccl::Camera *cam = scene->camera;
    cam->set_full_width(width);
    cam->set_full_height(height);
    cam->set_matrix(ccl::transform_translate(ccl::make_float3(0.0f, 1.4f, -6.5f))
                    * ccl::transform_rotate(0.15f, ccl::make_float3(1.0f, 0.0f, 0.0f)));
    cam->compute_auto_viewplane();
    cam->need_flags_update = true;
    cam->need_device_update = true;

    // A uniform sky is the only light.
    {
        ccl::Shader *shader = scene->default_background;
        auto graph = std::make_unique<ccl::ShaderGraph>();
        auto *bg = graph->create_node<ccl::BackgroundNode>();
        bg->set_color(ccl::make_float3(0.55f, 0.65f, 0.85f));
        bg->set_strength(1.0f);
        graph->connect(bg->output("Background"), graph->output()->input("Surface"));
        shader->set_graph(std::move(graph));
        shader->tag_update(scene);
    }

    ccl::Shader *cubeShader = makeDiffuse(scene, "cube", ccl::make_float3(0.8f, 0.12f, 0.1f));
    ccl::Shader *floorShader = makeDiffuse(scene, "floor", ccl::make_float3(0.6f, 0.6f, 0.6f));

    // A unit cube, turned so three faces show, on a floor at y = -1.
    const std::vector<ccl::float3> cubeVerts = {
        ccl::make_float3(-1, -1, -1), ccl::make_float3(1, -1, -1),
        ccl::make_float3(1, 1, -1),   ccl::make_float3(-1, 1, -1),
        ccl::make_float3(-1, -1, 1),  ccl::make_float3(1, -1, 1),
        ccl::make_float3(1, 1, 1),    ccl::make_float3(-1, 1, 1),
    };
    const std::vector<int> cubeTris = {
        0, 2, 1, 0, 3, 2,  // -z
        4, 5, 6, 4, 6, 7,  // +z
        0, 1, 5, 0, 5, 4,  // -y
        3, 7, 6, 3, 6, 2,  // +y
        0, 4, 7, 0, 7, 3,  // -x
        1, 2, 6, 1, 6, 5,  // +x
    };
    addMesh(scene, cubeShader, cubeVerts, cubeTris,
            ccl::transform_rotate(0.55f, ccl::make_float3(0.0f, 1.0f, 0.0f)));

    const float f = 10.0f;
    const std::vector<ccl::float3> floorVerts = {
        ccl::make_float3(-f, -1, -f), ccl::make_float3(f, -1, -f),
        ccl::make_float3(f, -1, f),   ccl::make_float3(-f, -1, f),
    };
    const std::vector<int> floorTris = {0, 1, 2, 0, 2, 3};
    addMesh(scene, floorShader, floorVerts, floorTris, ccl::transform_identity());

    ccl::Pass *pass = scene->create_node<ccl::Pass>();
    pass->set_name(ccl::ustring("combined"));
    pass->set_type(ccl::PASS_COMBINED);

    ccl::BufferParams bp;
    bp.width = width;
    bp.height = height;
    bp.full_width = width;
    bp.full_height = height;

    session->reset(sp, bp);
    session->start();
    session->wait();

    if (session->progress.get_error())
        return fail("Cycles: " + session->progress.get_error_message());
    if (!capture->done)
        return fail("Cycles produced no frame");

    // Bottom-up linear float to a top-down encoded PNG.
    QImage image(capture->width, capture->height, QImage::Format_RGB888);
    for (int y = 0; y < capture->height; ++y) {
        const float *src = capture->pixels.data()
                           + size_t(capture->height - 1 - y) * size_t(capture->width) * 4;
        unsigned char *dst = image.scanLine(y);
        for (int x = 0; x < capture->width; ++x, src += 4, dst += 3) {
            dst[0] = encode(src[0]);
            dst[1] = encode(src[1]);
            dst[2] = encode(src[2]);
        }
    }
    if (!image.save(QString::fromStdString(path)))
        return fail("could not write " + path);
    return true;
}

#endif  // HAVE_CYCLES

}  // namespace Render::Cycles
